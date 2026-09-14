#include "gtosd/equity/evaluator.hpp"
#include "gtosd/preflop/hu_preflop_legacy.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>

namespace gtosd {
namespace {

constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::uint64_t physical_private_deals = 630U * 561U;
constexpr std::uint64_t physical_flops = 7'140U;
constexpr std::uint64_t flops_per_private_deal = 4'960U;
constexpr std::uint64_t ordered_public_runouts_per_private_deal =
    flops_per_private_deal * 29U * 28U;
constexpr std::uint64_t physical_unordered_five_card_boards = 376'992U;
constexpr std::uint64_t ordered_histories_per_unordered_board = 20U;
constexpr std::uint64_t live_private_combos_per_complete_board = 465U;
constexpr std::uint64_t compatible_opponent_combos_per_complete_board = 406U;
constexpr std::uint64_t ordered_matchup_outcomes_per_physical_board =
    live_private_combos_per_complete_board * compatible_opponent_combos_per_complete_board *
    ordered_histories_per_unordered_board;
constexpr std::size_t all_in_matchup_count =
    hu_preflop_hand_class_count * hu_preflop_hand_class_count;
constexpr std::string_view all_in_evaluator_contract =
    "short_deck_36_flush_over_full_house_a6789_exact_v1";
constexpr std::uint64_t live_combos_per_flop = 528U;
constexpr std::uint64_t compatible_deals_per_flop = 528U * 465U;
constexpr std::uint64_t dense_boundary_record_bytes = 24U;
constexpr std::uint64_t river_live_combos_per_board = 465U;
constexpr double probability_tolerance = 1.0e-12;

std::uint64_t fnv1a(const std::string_view value, std::uint64_t hash = fnv_offset) {
  for (const auto character : value) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= fnv_prime;
  }
  return hash;
}

std::string hex64(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16U, '0');
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[result.size() - 1U - index] = digits[static_cast<std::size_t>(value & 0xFU)];
    value >>= 4U;
  }
  return result;
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

std::uint64_t whole_game_boundary_profile_slot_hash(const std::size_t task,
                                                    const std::uint8_t resolver,
                                                    const std::string_view boundary_fingerprint) {
  const auto identity = "gtosd.hu_preflop_continuation_slot.v1|" + std::to_string(task) + "|" +
                        std::to_string(resolver) + "|" + std::string(boundary_fingerprint);
  return fnv1a(identity);
}

bool checked_multiply(const std::uint64_t first, const std::uint64_t second,
                      std::uint64_t &result) {
  if (first != 0U && second > std::numeric_limits<std::uint64_t>::max() / first) {
    return false;
  }
  result = first * second;
  return true;
}

bool checked_add(const std::uint64_t first, const std::uint64_t second, std::uint64_t &result) {
  if (second > std::numeric_limits<std::uint64_t>::max() - first) {
    return false;
  }
  result = first + second;
  return true;
}

std::array<CardId, 3> canonical_flop(std::array<CardId, 3> flop) {
  std::ranges::sort(flop, {}, &CardId::value);
  return flop;
}

bool valid_flop(const std::array<CardId, 3> &flop) {
  return std::ranges::all_of(flop, [](const CardId card) { return card.value() < 36U; }) &&
         flop[0] != flop[1] && flop[0] != flop[2] && flop[1] != flop[2];
}

std::uint64_t flop_mask(const std::array<CardId, 3> &flop) {
  return flop[0].mask() | flop[1].mask() | flop[2].mask();
}

std::array<std::uint64_t, 630U> combo_masks() {
  std::array<std::uint64_t, 630U> result{};
  const auto combos = all_combos();
  for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
    result[combo] = combos[combo].first.mask() | combos[combo].second.mask();
  }
  return result;
}

double compatible_joint_mass(const std::array<Combo, 630U> &combos,
                             const std::array<std::uint64_t, 630U> &masks,
                             const HuPreflopComboReach &first_reach,
                             const HuPreflopComboReach &second_reach,
                             const std::uint64_t board_mask = 0U) {
  std::array<double, 36U> second_mass_by_card{};
  double second_total = 0.0;
  for (std::size_t second = 0U; second < combos.size(); ++second) {
    if ((masks[second] & board_mask) != 0U) {
      continue;
    }
    second_total += second_reach[second];
    second_mass_by_card[combos[second].first.value()] += second_reach[second];
    second_mass_by_card[combos[second].second.value()] += second_reach[second];
  }
  double result = 0.0;
  for (std::size_t first = 0U; first < combos.size(); ++first) {
    if ((masks[first] & board_mask) != 0U || first_reach[first] == 0.0) {
      continue;
    }
    const auto compatible_second_mass =
        second_total - second_mass_by_card[combos[first].first.value()] -
        second_mass_by_card[combos[first].second.value()] + second_reach[first];
    result += first_reach[first] * compatible_second_mass;
  }
  return result;
}

std::vector<HuPreflopCanonicalFlop> canonical_flop_catalog() {
  std::map<std::array<std::uint8_t, 3>, std::uint32_t> representatives;
  std::array<std::uint8_t, 4> permutation{0U, 1U, 2U, 3U};
  for (std::uint8_t first = 0U; first < 34U; ++first) {
    for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 35U; ++second) {
      for (std::uint8_t third = static_cast<std::uint8_t>(second + 1U); third < 36U; ++third) {
        std::array<std::uint8_t, 3> best{first, second, third};
        permutation = {0U, 1U, 2U, 3U};
        do {
          std::array<std::uint8_t, 3> transformed{
              static_cast<std::uint8_t>((first / 4U) * 4U + permutation[first % 4U]),
              static_cast<std::uint8_t>((second / 4U) * 4U + permutation[second % 4U]),
              static_cast<std::uint8_t>((third / 4U) * 4U + permutation[third % 4U])};
          std::ranges::sort(transformed);
          best = std::min(best, transformed);
        } while (std::ranges::next_permutation(permutation).found);
        ++representatives[best];
      }
    }
  }
  std::vector<HuPreflopCanonicalFlop> result;
  result.reserve(representatives.size());
  for (const auto &[representative, count] : representatives) {
    result.push_back({{CardId::from_index(representative[0]).value(),
                       CardId::from_index(representative[1]).value(),
                       CardId::from_index(representative[2]).value()},
                      count});
  }
  return result;
}

std::array<std::uint8_t, 5> canonical_all_in_board(std::array<std::uint8_t, 5> board) {
  std::ranges::sort(board);
  auto best = board;
  std::array<std::uint8_t, 4> permutation{0U, 1U, 2U, 3U};
  do {
    auto transformed = board;
    for (auto &card : transformed) {
      card = static_cast<std::uint8_t>((card / 4U) * 4U + permutation[card % 4U]);
    }
    std::ranges::sort(transformed);
    best = std::min(best, transformed);
  } while (std::ranges::next_permutation(permutation).found);
  return best;
}

std::vector<HuPreflopCanonicalAllInBoard> canonical_all_in_board_catalog() {
  std::map<std::array<std::uint8_t, 5>, std::uint32_t> representatives;
  for (std::uint8_t first = 0U; first < 32U; ++first) {
    for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 33U; ++second) {
      for (std::uint8_t third = static_cast<std::uint8_t>(second + 1U); third < 34U; ++third) {
        for (std::uint8_t fourth = static_cast<std::uint8_t>(third + 1U); fourth < 35U; ++fourth) {
          for (std::uint8_t fifth = static_cast<std::uint8_t>(fourth + 1U); fifth < 36U; ++fifth) {
            ++representatives[canonical_all_in_board({first, second, third, fourth, fifth})];
          }
        }
      }
    }
  }
  std::vector<HuPreflopCanonicalAllInBoard> result;
  result.reserve(representatives.size());
  for (const auto &[representative, count] : representatives) {
    HuPreflopCanonicalAllInBoard board;
    for (std::size_t index = 0U; index < board.cards.size(); ++index) {
      board.cards[index] = CardId::from_index(representative[index]).value();
    }
    board.physical_board_count = count;
    result.push_back(board);
  }
  return result;
}

struct CanonicalOrderedRunout {
  std::array<CardId, 2> cards{};
  std::uint32_t physical_outcome_count{0};
};

using SuitPermutation = std::array<std::uint8_t, 4>;

std::vector<SuitPermutation> flop_suit_stabilizer(const std::array<CardId, 3> &flop) {
  std::vector<SuitPermutation> result;
  SuitPermutation permutation{0U, 1U, 2U, 3U};
  const std::array<std::uint8_t, 3> source{flop[0].value(), flop[1].value(), flop[2].value()};
  do {
    auto transformed = source;
    for (auto &card : transformed) {
      card = static_cast<std::uint8_t>((card / 4U) * 4U + permutation[card % 4U]);
    }
    std::ranges::sort(transformed);
    if (transformed == source) {
      result.push_back(permutation);
    }
  } while (std::ranges::next_permutation(permutation).found);
  return result;
}

Result<std::vector<SuitPermutation>, HuPreflopError>
ordered_runout_orbit_lifts(const std::array<CardId, 3> &flop, const CardId turn,
                           const CardId river) {
  if (!valid_flop(flop) || turn == river || (turn.mask() & flop_mask(flop)) != 0U ||
      (river.mask() & flop_mask(flop)) != 0U) {
    return Result<std::vector<SuitPermutation>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto stabilizer = flop_suit_stabilizer(flop);
  std::map<std::array<std::uint8_t, 2>, SuitPermutation> unique_lifts;
  std::array<std::uint8_t, 2> representative{turn.value(), river.value()};
  for (const auto &permutation : stabilizer) {
    const std::array<std::uint8_t, 2> transformed{
        static_cast<std::uint8_t>((turn.value() / 4U) * 4U + permutation[turn.value() % 4U]),
        static_cast<std::uint8_t>((river.value() / 4U) * 4U + permutation[river.value() % 4U])};
    representative = std::min(representative, transformed);
    unique_lifts.try_emplace(transformed, permutation);
  }
  if (stabilizer.empty() || representative != std::array{turn.value(), river.value()} ||
      unique_lifts.empty()) {
    return Result<std::vector<SuitPermutation>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  std::vector<SuitPermutation> result;
  result.reserve(unique_lifts.size());
  for (const auto &[cards, permutation] : unique_lifts) {
    static_cast<void>(cards);
    result.push_back(permutation);
  }
  return Result<std::vector<SuitPermutation>, HuPreflopError>::success(std::move(result));
}

Result<std::vector<SuitPermutation>, HuPreflopError>
turn_orbit_lifts(const std::array<CardId, 3> &flop, const CardId turn) {
  if (!valid_flop(flop) || (turn.mask() & flop_mask(flop)) != 0U) {
    return Result<std::vector<SuitPermutation>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto stabilizer = flop_suit_stabilizer(flop);
  std::map<std::uint8_t, SuitPermutation> unique_lifts;
  auto representative = turn.value();
  for (const auto &permutation : stabilizer) {
    const auto transformed =
        static_cast<std::uint8_t>((turn.value() / 4U) * 4U + permutation[turn.value() % 4U]);
    representative = std::min(representative, transformed);
    unique_lifts.try_emplace(transformed, permutation);
  }
  if (stabilizer.empty() || representative != turn.value() || unique_lifts.empty()) {
    return Result<std::vector<SuitPermutation>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  std::vector<SuitPermutation> result;
  result.reserve(unique_lifts.size());
  for (const auto &[card, permutation] : unique_lifts) {
    static_cast<void>(card);
    result.push_back(permutation);
  }
  return Result<std::vector<SuitPermutation>, HuPreflopError>::success(std::move(result));
}

ComboId transform_combo(const ComboId combo, const SuitPermutation &permutation) {
  constexpr auto invalid = std::numeric_limits<ComboId>::max();
  static const auto lookup = [] {
    std::array<std::array<ComboId, 36U>, 36U> result{};
    for (auto &row : result) {
      row.fill(std::numeric_limits<ComboId>::max());
    }
    const auto combos = all_combos();
    for (std::size_t index = 0U; index < combos.size(); ++index) {
      const auto first = combos[index].first.value();
      const auto second = combos[index].second.value();
      result[first][second] = static_cast<ComboId>(index);
      result[second][first] = static_cast<ComboId>(index);
    }
    return result;
  }();
  const auto combos = all_combos();
  if (combo >= combos.size()) {
    return invalid;
  }
  const auto transform_card = [&](const CardId card) {
    return static_cast<std::uint8_t>((card.value() / 4U) * 4U + permutation[card.value() % 4U]);
  };
  return lookup[transform_card(combos[combo].first)][transform_card(combos[combo].second)];
}

Result<std::vector<CanonicalOrderedRunout>, HuPreflopError>
canonical_ordered_runouts(const std::array<CardId, 3> &flop) {
  const auto stabilizer = flop_suit_stabilizer(flop);
  if (stabilizer.empty()) {
    return Result<std::vector<CanonicalOrderedRunout>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  const auto board_mask = flop_mask(flop);
  std::map<std::array<std::uint8_t, 2>, std::uint32_t> representatives;
  for (const auto turn : short_deck()) {
    if ((turn.mask() & board_mask) != 0U) {
      continue;
    }
    for (const auto river : short_deck()) {
      if (river == turn || (river.mask() & board_mask) != 0U) {
        continue;
      }
      std::array<std::uint8_t, 2> best{turn.value(), river.value()};
      for (const auto &mapping : stabilizer) {
        const std::array<std::uint8_t, 2> candidate{
            static_cast<std::uint8_t>((turn.value() / 4U) * 4U + mapping[turn.value() % 4U]),
            static_cast<std::uint8_t>((river.value() / 4U) * 4U + mapping[river.value() % 4U])};
        best = std::min(best, candidate);
      }
      ++representatives[best];
    }
  }
  std::vector<CanonicalOrderedRunout> result;
  result.reserve(representatives.size());
  for (const auto &[representative, count] : representatives) {
    result.push_back({{CardId::from_index(representative[0]).value(),
                       CardId::from_index(representative[1]).value()},
                      count});
  }
  return result.empty() ? Result<std::vector<CanonicalOrderedRunout>, HuPreflopError>::failure(
                              HuPreflopError::IntegrityFailure)
                        : Result<std::vector<CanonicalOrderedRunout>, HuPreflopError>::success(
                              std::move(result));
}

const HuPreflopPostflopEntryReach *find_entry(const HuPreflopDecompositionPlan &plan,
                                              const std::uint32_t entry_node) {
  const auto found =
      std::ranges::find(plan.entries, entry_node, &HuPreflopPostflopEntryReach::entry_node);
  return found == plan.entries.end() ? nullptr : &*found;
}

std::string plan_fingerprint(const HuPreflopDecompositionPlan &plan) {
  std::ostringstream serialized;
  serialized << std::setprecision(std::numeric_limits<double>::max_digits10)
             << "gtosd.hu_preflop_decomposition.v1|" << plan.major << '|' << plan.minor << '|'
             << plan.tree_fingerprint << '|' << plan.blueprint_fingerprint << '|'
             << plan.physical_private_deals << '|' << plan.physical_flops << '|'
             << plan.canonical_flops << '|' << plan.public_flop_roots << '|'
             << plan.canonical_public_flop_roots << '|' << plan.physical_deal_flop_histories << '|'
             << plan.postflop_entry_probability << '|' << plan.terminal_fold_probability << '|'
             << plan.terminal_all_in_probability << '|' << plan.total_probability << '|'
             << plan.maximum_live_combos_per_player << '|' << plan.maximum_compatible_deals_per_flop
             << '|' << plan.bytes.reach_template_bytes << '|'
             << plan.bytes.one_flop_conditioned_range_bytes << '|'
             << plan.bytes.one_resolver_boundary_bytes << '|'
             << plan.bytes.all_resolver_boundaries_bytes << '|'
             << plan.bytes.both_players_boundary_bytes << '|'
             << plan.bytes.canonical_all_resolver_boundaries_bytes << '|'
             << plan.bytes.canonical_both_players_boundary_bytes << '|'
             << plan.bytes.fully_materialized_range_bytes << '|'
             << plan.bytes.canonical_fully_materialized_range_bytes << '|'
             << plan.bytes.whole_game_coverage_mask_bytes << '|'
             << plan.bytes.whole_game_coverage_probability_bytes << '|'
             << plan.bytes.whole_game_profile_utility_bytes << '|'
             << plan.bytes.whole_game_local_continuation_identity_bytes << '|'
             << plan.bytes.whole_game_coverage_payload_bytes << '|'
             << plan.bytes.streaming_certification_live_payload_bytes << '|';
  for (const auto &entry : plan.entries) {
    serialized << entry.entry_node << ':' << entry.joint_entry_probability << ':';
    for (const auto &player : entry.own_sequence_reach) {
      for (const auto reach : player) {
        serialized << reach << ',';
      }
    }
  }
  for (const auto &flop : plan.canonical_flop_catalog) {
    for (const auto card : flop.cards) {
      serialized << static_cast<unsigned>(card.value()) << ',';
    }
    serialized << flop.physical_outcome_count << ';';
  }
  return "fnv1a64:" + hex64(fnv1a(serialized.str()));
}

std::string boundary_fingerprint(const HuPreflopFlopBoundary &boundary) {
  std::ostringstream serialized;
  serialized << "gtosd.hu_preflop_flop_boundary.v2|" << boundary.major << '|' << boundary.minor
             << '|' << boundary.tree_fingerprint << '|' << boundary.blueprint_fingerprint << '|'
             << boundary.continuation_fingerprint << '|'
             << boundary.continuation_checkpoint_fingerprint << '|' << boundary.blueprint_iterations
             << '|' << boundary.entry_node << '|'
             << static_cast<unsigned>(boundary.resolving_player) << '|'
             << static_cast<unsigned>(boundary.opponent) << '|'
             << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto card : boundary.flop) {
    serialized << static_cast<unsigned>(card.value()) << ',';
  }
  for (const auto &value : boundary.values) {
    serialized << value.opponent_combo << ':' << value.counterfactual_reach << ':'
               << value.blueprint_counterfactual_value_antes << ':' << value.positive_reach << ';';
  }
  return "fnv1a64:" + hex64(fnv1a(serialized.str()));
}

std::string river_work_fingerprint(const std::string &tree_fingerprint,
                                   const std::string &plan_fingerprint,
                                   const HuPreflopRiverWorkEstimate &work) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_work_estimate.v5|" << tree_fingerprint << '|'
           << plan_fingerprint << '|' << work.physical_ordered_runouts_per_flop << '|'
           << work.physical_public_board_histories << '|' << work.canonical_public_board_histories
           << '|' << work.minimum_canonical_runouts_per_flop << '|'
           << work.maximum_canonical_runouts_per_flop << '|' << work.river_betting_histories << '|'
           << work.physical_resolver_roots << '|' << work.canonical_resolver_roots << '|'
           << work.canonical_roots_for_both_players << '|' << work.boundary_values_per_resolver_root
           << '|' << work.boundary_bytes_per_resolver_root << '|'
           << work.fully_materialized_boundary_bytes << '|'
           << work.maximum_best_response_turn_components_per_root << '|'
           << work.best_response_boundary_bytes_per_resolver_root << '|'
           << work.fully_materialized_best_response_boundary_bytes << '|'
           << work.maximum_canonical_river_roots_per_physical_turn_leaf << '|'
           << work.best_response_leaf_accumulator_value_bytes << '|'
           << work.maximum_best_response_leaf_manifest_bytes << '|'
           << work.maximum_best_response_leaf_live_payload_bytes << '|'
           << work.best_response_task_value_bytes << '|'
           << work.best_response_entry_accumulator_value_bytes << '|'
           << work.best_response_entry_reduction_live_payload_bytes << '|'
           << work.all_in_canonical_complete_boards << '|'
           << work.all_in_board_catalog_payload_bytes << '|'
           << work.all_in_matchup_table_payload_bytes << '|'
           << work.all_in_one_board_scratch_payload_bytes << '|'
           << work.all_in_streaming_live_payload_bytes;
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_batch_plan_fingerprint(const HuPreflopRiverBatchPlan &plan) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_batch_plan.v1|" << plan.major << '|' << plan.minor << '|'
           << plan.tree_fingerprint << '|' << plan.blueprint_fingerprint << '|'
           << plan.decomposition_plan_fingerprint << '|' << plan.river_work_fingerprint << '|'
           << plan.resolver_root_ordering << '|' << plan.resolver_roots << '|'
           << plan.target_batch_payload_bytes << '|' << plan.boundary_bytes_per_resolver_root << '|'
           << plan.roots_per_batch << '|' << plan.batch_count << '|' << plan.final_batch_root_count
           << '|' << plan.maximum_batch_payload_bytes << '|'
           << plan.fully_materialized_boundary_bytes << '|'
           << plan.reduce_to_upper_street_accumulator << '|'
           << plan.allow_full_boundary_materialization << '|';
  for (std::size_t index = 0U; index < plan.batch_first_resolver_roots.size(); ++index) {
    identity << plan.batch_first_resolver_roots[index] << ':'
             << plan.batch_resolver_root_counts[index] << ':' << plan.batch_task_span_indices[index]
             << ';';
  }
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_batch_fingerprint(const HuPreflopRiverBatch &batch) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_batch.v2|" << batch.index << '|' << batch.task_span_index
           << '|' << batch.first_resolver_root << '|' << batch.resolver_root_count << '|'
           << batch.maximum_boundary_payload_bytes << '|' << batch.batch_plan_fingerprint;
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_resolver_root_fingerprint(const HuPreflopRiverResolverRoot &root) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_resolver_root.v1|" << root.ordinal << '|'
           << root.task_span_index << '|' << root.task_first_resolver_root << '|'
           << root.task_resolver_root_count << '|' << root.entry_node << '|';
  for (const auto card : root.flop) {
    identity << static_cast<unsigned>(card.value()) << ',';
  }
  identity << '|' << static_cast<unsigned>(root.turn.value()) << '|'
           << static_cast<unsigned>(root.river.value()) << '|'
           << static_cast<unsigned>(root.resolving_player) << '|'
           << root.physical_public_outcome_count << '|' << serialize_public_state(root.state)
           << '|';
  for (const auto &action : root.action_history) {
    identity << static_cast<unsigned>(action.type) << ':' << action.amount.units() << ':'
             << action.requested_basis_points << ':' << static_cast<unsigned>(action.all_in_kind)
             << ',';
  }
  identity << '|' << root.history_fingerprint << '|' << root.batch_plan_fingerprint;
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_root_catalog_fingerprint(const HuPreflopRiverRootCatalog &catalog) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_root_catalog.v1|" << catalog.tree_fingerprint << '|'
           << catalog.decomposition_plan_fingerprint << '|' << catalog.river_work_fingerprint << '|'
           << catalog.physical_public_board_histories << '|' << catalog.resolver_roots << '|';
  for (const auto &shape : catalog.river_shapes) {
    identity << shape.entry_node << ':' << serialize_public_state(shape.state) << ':';
    for (const auto &action : shape.action_history) {
      identity << static_cast<unsigned>(action.type) << ':' << action.amount.units() << ':'
               << action.requested_basis_points << ':' << static_cast<unsigned>(action.all_in_kind)
               << ',';
    }
    identity << ':' << shape.history_fingerprint << ';';
  }
  identity << '|';
  for (const auto &board : catalog.canonical_boards) {
    for (const auto card : board.flop) {
      identity << static_cast<unsigned>(card.value()) << ',';
    }
    identity << static_cast<unsigned>(board.turn.value()) << ','
             << static_cast<unsigned>(board.river.value()) << ',' << board.physical_outcome_count
             << ';';
  }
  identity << '|';
  for (const auto &span : catalog.task_spans) {
    identity << span.first_resolver_root << ':' << span.resolver_root_count << ':'
             << span.entry_node << ':' << span.flop_catalog_index << ':' << span.first_shape_index
             << ':' << span.shape_count << ':' << span.first_board_index << ':' << span.board_count
             << ';';
  }
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_turn_group_fingerprint(const HuPreflopRiverTurnGroup &group) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_turn_group.v1|" << group.root_catalog_fingerprint << '|'
           << group.task_span_index << '|' << group.entry_node << '|';
  for (const auto card : group.flop) {
    identity << static_cast<unsigned>(card.value()) << ',';
  }
  identity << '|' << static_cast<unsigned>(group.turn.value()) << '|' << group.first_board_offset
           << '|' << group.canonical_board_count << '|' << group.physical_public_outcome_count;
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_turn_resolver_span_fingerprint(const HuPreflopRiverTurnResolverSpan &span) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_turn_resolver_span.v1|" << span.root_catalog_fingerprint
           << '|' << span.turn_group_fingerprint << '|' << span.task_span_index << '|'
           << span.shape_index << '|' << span.first_resolver_root << '|'
           << span.resolver_root_count;
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_root_boundary_fingerprint(const HuPreflopRiverRootBoundary &boundary) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_root_boundary.v5|" << boundary.major << '|' << boundary.minor
           << '|' << boundary.tree_fingerprint << '|' << boundary.blueprint_fingerprint << '|'
           << boundary.batch_plan_fingerprint << '|' << boundary.resolver_root_fingerprint << '|'
           << boundary.continuation_fingerprint << '|' << static_cast<unsigned>(boundary.value_mode)
           << '|' << boundary.blueprint_iterations << '|' << boundary.resolver_root_ordinal << '|'
           << boundary.task_span_index << '|' << boundary.entry_node << '|';
  for (const auto card : boundary.flop) {
    identity << static_cast<unsigned>(card.value()) << ',';
  }
  identity << static_cast<unsigned>(boundary.turn.value()) << '|'
           << static_cast<unsigned>(boundary.river.value()) << '|'
           << static_cast<unsigned>(boundary.resolving_player) << '|'
           << static_cast<unsigned>(boundary.opponent) << '|'
           << boundary.physical_public_outcome_count << '|'
           << std::setprecision(std::numeric_limits<double>::max_digits10);
  identity << boundary.best_response_turn_components.size() << '|';
  for (const auto &component : boundary.best_response_turn_components) {
    identity << static_cast<unsigned>(component.turn.value()) << ':' << component.values.size()
             << ':';
    for (const auto &value : component.values) {
      identity << value.opponent_combo << ',' << value.counterfactual_reach << ','
               << value.blueprint_counterfactual_value_antes << ',' << value.positive_reach << ';';
    }
    identity << '|';
  }
  for (const auto &value : boundary.values) {
    identity << value.opponent_combo << ':' << value.counterfactual_reach << ':'
             << value.blueprint_counterfactual_value_antes << ':' << value.positive_reach << ';';
  }
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_task_accumulator_fingerprint(const HuPreflopRiverTaskAccumulator &accumulator) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_task_accumulator.v5|" << accumulator.major << '|'
           << accumulator.minor << '|' << accumulator.tree_fingerprint << '|'
           << accumulator.blueprint_fingerprint << '|' << accumulator.root_catalog_fingerprint
           << '|' << accumulator.batch_plan_fingerprint << '|'
           << accumulator.continuation_checkpoint_fingerprint << '|'
           << static_cast<unsigned>(accumulator.value_mode) << '|'
           << accumulator.blueprint_iterations << '|' << accumulator.task_span_index << '|'
           << accumulator.first_resolver_root << '|' << accumulator.resolver_root_count << '|'
           << accumulator.next_resolver_root << '|' << accumulator.entry_node << '|';
  for (const auto card : accumulator.flop) {
    identity << static_cast<unsigned>(card.value()) << ',';
  }
  identity << '|' << accumulator.accumulated_roots_by_resolver[0] << '|'
           << accumulator.accumulated_roots_by_resolver[1] << '|' << accumulator.complete << '|'
           << accumulator.contribution_chain_fingerprint << '|'
           << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto &player : accumulator.values) {
    for (const auto &value : player) {
      identity << value.weighted_reach_sum << ':' << value.weighted_reach_compensation << ':'
               << value.weighted_utility_sum_antes << ':'
               << value.weighted_utility_compensation_antes << ';';
    }
  }
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

std::string river_task_aggregate_fingerprint(const HuPreflopRiverTaskAggregate &aggregate) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_task_aggregate.v5|" << aggregate.major << '|'
           << aggregate.minor << '|' << aggregate.tree_fingerprint << '|'
           << aggregate.blueprint_fingerprint << '|' << aggregate.accumulator_fingerprint << '|'
           << aggregate.continuation_checkpoint_fingerprint << '|'
           << static_cast<unsigned>(aggregate.value_mode) << '|' << aggregate.blueprint_iterations
           << '|' << aggregate.task_span_index << '|' << aggregate.entry_node << '|';
  for (const auto card : aggregate.flop) {
    identity << static_cast<unsigned>(card.value()) << ',';
  }
  identity << '|' << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto &player : aggregate.resolver_values) {
    for (const auto &value : player) {
      identity << value.opponent_combo << ':' << value.weighted_counterfactual_reach << ':'
               << value.weighted_counterfactual_utility_antes << ':'
               << value.conditional_value_antes << ':' << value.positive_reach << ';';
    }
  }
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

void fnv1a_u64(std::uint64_t &hash, std::uint64_t value) {
  for (std::size_t byte = 0U; byte < sizeof(value); ++byte) {
    hash ^= static_cast<std::uint8_t>(value & 0xFFU);
    hash *= fnv_prime;
    value >>= 8U;
  }
}

void fnv1a_text(std::uint64_t &hash, const std::string_view value) {
  fnv1a_u64(hash, static_cast<std::uint64_t>(value.size()));
  for (const auto character : value) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= fnv_prime;
  }
}

std::string upper_terminal_contribution_fingerprint(
    const HuPreflopUpperStreetTerminalContribution &contribution) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_upper_terminal_contribution.v2");
  fnv1a_u64(hash, contribution.major);
  fnv1a_u64(hash, contribution.minor);
  fnv1a_text(hash, contribution.tree_fingerprint);
  fnv1a_text(hash, contribution.blueprint_fingerprint);
  fnv1a_text(hash, contribution.continuation_checkpoint_fingerprint);
  fnv1a_u64(hash, contribution.blueprint_iterations);
  fnv1a_u64(hash, contribution.task_span_index);
  fnv1a_u64(hash, contribution.entry_node);
  for (const auto card : contribution.flop) {
    fnv1a_u64(hash, card.value());
  }
  fnv1a_u64(hash, contribution.terminal_ordinal);
  fnv1a_u64(hash, static_cast<std::uint64_t>(contribution.terminal_street));
  fnv1a_u64(hash, static_cast<std::uint64_t>(contribution.terminal_status));
  fnv1a_text(hash, contribution.terminal_history_fingerprint);
  for (const auto &resolver : contribution.resolver_values) {
    fnv1a_u64(hash, static_cast<std::uint64_t>(resolver.size()));
    for (const auto &value : resolver) {
      fnv1a_u64(hash, value.opponent_combo);
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_reach));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_utility_antes));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.conditional_value_antes));
      fnv1a_u64(hash, value.positive_reach ? 1U : 0U);
    }
  }
  return "fnv1a64:" + hex64(hash);
}

std::string upper_best_response_terminal_contribution_fingerprint(
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_upper_best_response_terminal_contribution.v2");
  fnv1a_u64(hash, contribution.major);
  fnv1a_u64(hash, contribution.minor);
  fnv1a_text(hash, contribution.tree_fingerprint);
  fnv1a_text(hash, contribution.blueprint_fingerprint);
  fnv1a_text(hash, contribution.root_catalog_fingerprint);
  fnv1a_text(hash, contribution.turn_group_fingerprint);
  fnv1a_text(hash, contribution.continuation_fingerprint);
  fnv1a_u64(hash, contribution.blueprint_iterations);
  fnv1a_u64(hash, contribution.task_span_index);
  fnv1a_u64(hash, contribution.entry_node);
  for (const auto card : contribution.flop) {
    fnv1a_u64(hash, card.value());
  }
  fnv1a_u64(hash, contribution.turn.value());
  fnv1a_u64(hash, contribution.has_turn ? 1U : 0U);
  fnv1a_u64(hash, contribution.responding_player);
  fnv1a_u64(hash, contribution.terminal_ordinal);
  fnv1a_u64(hash, static_cast<std::uint64_t>(contribution.terminal_street));
  fnv1a_u64(hash, static_cast<std::uint64_t>(contribution.terminal_status));
  fnv1a_text(hash, contribution.terminal_history_fingerprint);
  fnv1a_u64(hash, static_cast<std::uint64_t>(contribution.turn_components.size()));
  for (const auto &component : contribution.turn_components) {
    fnv1a_u64(hash, component.turn.value());
    fnv1a_u64(hash, static_cast<std::uint64_t>(component.values.size()));
    for (const auto &value : component.values) {
      fnv1a_u64(hash, value.responding_combo);
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_reach));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_utility_antes));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.conditional_value_antes));
      fnv1a_u64(hash, value.positive_reach ? 1U : 0U);
    }
  }
  fnv1a_u64(hash, static_cast<std::uint64_t>(contribution.values.size()));
  for (const auto &value : contribution.values) {
    fnv1a_u64(hash, value.responding_combo);
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_reach));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_utility_antes));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.conditional_value_antes));
    fnv1a_u64(hash, value.positive_reach ? 1U : 0U);
  }
  return "fnv1a64:" + hex64(hash);
}

std::string
best_response_task_evaluation_fingerprint(const HuPreflopBestResponseTaskEvaluation &evaluation) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_best_response_task_evaluation.v1");
  fnv1a_u64(hash, evaluation.major);
  fnv1a_u64(hash, evaluation.minor);
  fnv1a_text(hash, evaluation.tree_fingerprint);
  fnv1a_text(hash, evaluation.blueprint_fingerprint);
  fnv1a_text(hash, evaluation.continuation_fingerprint);
  fnv1a_u64(hash, evaluation.blueprint_iterations);
  fnv1a_u64(hash, evaluation.task_span_index);
  fnv1a_u64(hash, evaluation.entry_node);
  for (const auto card : evaluation.flop) {
    fnv1a_u64(hash, card.value());
  }
  fnv1a_u64(hash, evaluation.responding_player);
  fnv1a_u64(hash, evaluation.decision_nodes_visited);
  fnv1a_u64(hash, evaluation.responder_decision_nodes_visited);
  fnv1a_u64(hash, evaluation.opponent_decision_nodes_visited);
  fnv1a_u64(hash, evaluation.physical_turn_branches_visited);
  fnv1a_u64(hash, evaluation.upper_terminal_leaves_visited);
  fnv1a_u64(hash, evaluation.river_continuation_leaves_visited);
  fnv1a_u64(hash, evaluation.maximum_recursion_depth);
  fnv1a_u64(hash, static_cast<std::uint64_t>(evaluation.values.size()));
  for (const auto &value : evaluation.values) {
    fnv1a_u64(hash, value.responding_combo);
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_reach));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_utility_antes));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.conditional_value_antes));
    fnv1a_u64(hash, value.positive_reach ? 1U : 0U);
  }
  return "fnv1a64:" + hex64(hash);
}

std::string best_response_entry_accumulator_fingerprint(
    const HuPreflopBestResponseEntryAccumulator &accumulator) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_best_response_entry_accumulator.v1");
  fnv1a_u64(hash, accumulator.major);
  fnv1a_u64(hash, accumulator.minor);
  fnv1a_text(hash, accumulator.tree_fingerprint);
  fnv1a_text(hash, accumulator.blueprint_fingerprint);
  fnv1a_text(hash, accumulator.continuation_fingerprint);
  fnv1a_u64(hash, accumulator.blueprint_iterations);
  fnv1a_u64(hash, accumulator.entry_index);
  fnv1a_u64(hash, accumulator.entry_node);
  fnv1a_u64(hash, accumulator.responding_player);
  fnv1a_u64(hash, accumulator.first_task_span_index);
  fnv1a_u64(hash, accumulator.task_count);
  fnv1a_u64(hash, accumulator.next_task_offset);
  for (const auto &value : accumulator.values) {
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_reach_sum));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_reach_compensation));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_utility_sum_antes));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_utility_compensation_antes));
  }
  fnv1a_u64(hash, accumulator.complete ? 1U : 0U);
  fnv1a_text(hash, accumulator.contribution_chain_fingerprint);
  return "fnv1a64:" + hex64(hash);
}

std::string
best_response_entry_evaluation_fingerprint(const HuPreflopBestResponseEntryEvaluation &evaluation) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_best_response_entry_evaluation.v1");
  fnv1a_u64(hash, evaluation.major);
  fnv1a_u64(hash, evaluation.minor);
  fnv1a_text(hash, evaluation.tree_fingerprint);
  fnv1a_text(hash, evaluation.blueprint_fingerprint);
  fnv1a_text(hash, evaluation.continuation_fingerprint);
  fnv1a_text(hash, evaluation.accumulator_fingerprint);
  fnv1a_u64(hash, evaluation.blueprint_iterations);
  fnv1a_u64(hash, evaluation.entry_index);
  fnv1a_u64(hash, evaluation.entry_node);
  fnv1a_u64(hash, evaluation.responding_player);
  fnv1a_u64(hash, evaluation.task_count);
  fnv1a_u64(hash, static_cast<std::uint64_t>(evaluation.values.size()));
  for (const auto &value : evaluation.values) {
    fnv1a_u64(hash, value.responding_class);
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_reach));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_utility_antes));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.conditional_value_antes));
    fnv1a_u64(hash, value.positive_reach ? 1U : 0U);
  }
  return "fnv1a64:" + hex64(hash);
}

std::string best_response_entry_initial_chain(const std::uint64_t entry_index,
                                              const std::uint8_t player) {
  return "fnv1a64:" + hex64(fnv1a("gtosd.hu_preflop_best_response_entry_contributions.v1|" +
                                  std::to_string(entry_index) + "|" + std::to_string(player)));
}

std::string best_response_preflop_evaluation_fingerprint(
    const HuPreflopBestResponsePreflopEvaluation &evaluation) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_best_response_preflop_evaluation.v1");
  fnv1a_u64(hash, evaluation.major);
  fnv1a_u64(hash, evaluation.minor);
  fnv1a_text(hash, evaluation.tree_fingerprint);
  fnv1a_text(hash, evaluation.blueprint_fingerprint);
  fnv1a_text(hash, evaluation.continuation_fingerprint);
  fnv1a_u64(hash, evaluation.blueprint_iterations);
  fnv1a_u64(hash, evaluation.responding_player);
  fnv1a_u64(hash, evaluation.decision_nodes_visited);
  fnv1a_u64(hash, evaluation.responder_decision_nodes_visited);
  fnv1a_u64(hash, evaluation.opponent_decision_nodes_visited);
  fnv1a_u64(hash, evaluation.postflop_entry_leaves_visited);
  fnv1a_u64(hash, evaluation.terminal_fold_leaves_visited);
  fnv1a_u64(hash, evaluation.terminal_all_in_leaves_visited);
  fnv1a_u64(hash, evaluation.maximum_recursion_depth);
  fnv1a_u64(hash, static_cast<std::uint64_t>(evaluation.values.size()));
  for (const auto &value : evaluation.values) {
    fnv1a_u64(hash, value.responding_class);
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_reach));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_utility_antes));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.conditional_value_antes));
    fnv1a_u64(hash, value.positive_reach ? 1U : 0U);
  }
  return "fnv1a64:" + hex64(hash);
}

std::string
exact_profile_evaluation_fingerprint(const HuPreflopExactProfileEvaluation &evaluation) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_exact_profile_evaluation.v1");
  fnv1a_u64(hash, evaluation.major);
  fnv1a_u64(hash, evaluation.minor);
  fnv1a_text(hash, evaluation.tree_fingerprint);
  fnv1a_text(hash, evaluation.blueprint_fingerprint);
  fnv1a_text(hash, evaluation.plan_fingerprint);
  fnv1a_text(hash, evaluation.continuation_profile_fingerprint);
  fnv1a_text(hash, evaluation.all_in_equity_table_fingerprint);
  fnv1a_u64(hash, evaluation.blueprint_iterations);
  fnv1a_u64(hash, evaluation.chance_outcome_count);
  fnv1a_u64(hash, evaluation.postflop_boundary_count);
  fnv1a_u64(hash, evaluation.terminal_fold_count);
  fnv1a_u64(hash, evaluation.terminal_all_in_count);
  for (const auto value : evaluation.postflop_values_antes) {
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value));
  }
  for (const auto value : evaluation.preflop_terminal_values_antes) {
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value));
  }
  for (const auto value : evaluation.total_values_antes) {
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value));
  }
  return "fnv1a64:" + hex64(hash);
}

std::string best_response_preflop_terminal_evaluation_fingerprint(
    const HuPreflopBestResponsePreflopTerminalEvaluation &evaluation) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_best_response_preflop_terminal_evaluation.v1");
  fnv1a_u64(hash, evaluation.major);
  fnv1a_u64(hash, evaluation.minor);
  fnv1a_text(hash, evaluation.tree_fingerprint);
  fnv1a_text(hash, evaluation.blueprint_fingerprint);
  fnv1a_text(hash, evaluation.continuation_fingerprint);
  fnv1a_u64(hash, evaluation.blueprint_iterations);
  fnv1a_u64(hash, evaluation.node_id);
  fnv1a_u64(hash, static_cast<std::uint64_t>(evaluation.kind));
  fnv1a_u64(hash, evaluation.responding_player);
  fnv1a_u64(hash, evaluation.ordered_public_runouts_per_private_deal);
  fnv1a_u64(hash, static_cast<std::uint64_t>(evaluation.values.size()));
  for (const auto &value : evaluation.values) {
    fnv1a_u64(hash, value.responding_class);
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_reach));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_counterfactual_utility_antes));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.conditional_value_antes));
    fnv1a_u64(hash, value.positive_reach ? 1U : 0U);
  }
  return "fnv1a64:" + hex64(hash);
}

std::string all_in_board_catalog_fingerprint(const HuPreflopAllInBoardCatalog &catalog) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_all_in_board_catalog.v1");
  fnv1a_u64(hash, catalog.major);
  fnv1a_u64(hash, catalog.minor);
  fnv1a_text(hash, catalog.evaluator_contract);
  fnv1a_u64(hash, catalog.physical_unordered_boards);
  fnv1a_u64(hash, catalog.ordered_histories_per_unordered_board);
  fnv1a_u64(hash, static_cast<std::uint64_t>(catalog.boards.size()));
  for (const auto &board : catalog.boards) {
    for (const auto card : board.cards) {
      fnv1a_u64(hash, card.value());
    }
    fnv1a_u64(hash, board.physical_board_count);
  }
  return "fnv1a64:" + hex64(hash);
}

std::string
all_in_equity_accumulator_fingerprint(const HuPreflopAllInEquityAccumulator &accumulator) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_all_in_equity_accumulator.v1");
  fnv1a_u64(hash, accumulator.major);
  fnv1a_u64(hash, accumulator.minor);
  fnv1a_text(hash, accumulator.board_catalog_fingerprint);
  fnv1a_text(hash, accumulator.evaluator_contract);
  fnv1a_u64(hash, accumulator.next_board_ordinal);
  fnv1a_u64(hash, accumulator.processed_physical_unordered_boards);
  fnv1a_u64(hash, accumulator.matchup_outcome_count);
  fnv1a_u64(hash, static_cast<std::uint64_t>(accumulator.matchups.size()));
  for (const auto &matchup : accumulator.matchups) {
    fnv1a_u64(hash, matchup.responding_player_wins);
    fnv1a_u64(hash, matchup.ties);
    fnv1a_u64(hash, matchup.responding_player_losses);
  }
  fnv1a_u64(hash, accumulator.complete ? 1U : 0U);
  fnv1a_text(hash, accumulator.contribution_chain_fingerprint);
  return "fnv1a64:" + hex64(hash);
}

std::string all_in_equity_table_fingerprint(const HuPreflopAllInEquityTable &table) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_all_in_equity_table.v1");
  fnv1a_u64(hash, table.major);
  fnv1a_u64(hash, table.minor);
  fnv1a_text(hash, table.board_catalog_fingerprint);
  fnv1a_text(hash, table.evaluator_contract);
  fnv1a_u64(hash, table.canonical_board_count);
  fnv1a_u64(hash, table.physical_unordered_boards);
  fnv1a_u64(hash, table.ordered_public_runouts_per_private_deal);
  fnv1a_u64(hash, table.matchup_outcome_count);
  fnv1a_u64(hash, static_cast<std::uint64_t>(table.matchups.size()));
  for (const auto &matchup : table.matchups) {
    fnv1a_u64(hash, matchup.responding_player_wins);
    fnv1a_u64(hash, matchup.ties);
    fnv1a_u64(hash, matchup.responding_player_losses);
  }
  fnv1a_text(hash, table.accumulator_fingerprint);
  return "fnv1a64:" + hex64(hash);
}

std::string river_best_response_leaf_accumulator_fingerprint(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator) {
  auto hash = fnv_offset;
  fnv1a_text(hash, "gtosd.hu_preflop_river_best_response_leaf_accumulator.v1");
  fnv1a_u64(hash, accumulator.major);
  fnv1a_u64(hash, accumulator.minor);
  fnv1a_text(hash, accumulator.tree_fingerprint);
  fnv1a_text(hash, accumulator.blueprint_fingerprint);
  fnv1a_text(hash, accumulator.root_catalog_fingerprint);
  fnv1a_text(hash, accumulator.batch_plan_fingerprint);
  fnv1a_text(hash, accumulator.continuation_fingerprint);
  fnv1a_u64(hash, accumulator.blueprint_iterations);
  fnv1a_u64(hash, accumulator.task_span_index);
  fnv1a_u64(hash, accumulator.entry_node);
  for (const auto card : accumulator.flop) {
    fnv1a_u64(hash, card.value());
  }
  fnv1a_u64(hash, accumulator.turn.value());
  fnv1a_u64(hash, accumulator.responding_player);
  fnv1a_text(hash, serialize_public_state(accumulator.state));
  fnv1a_u64(hash, static_cast<std::uint64_t>(accumulator.action_history.size()));
  for (const auto &action : accumulator.action_history) {
    fnv1a_u64(hash, static_cast<std::uint64_t>(action.type));
    fnv1a_u64(hash, static_cast<std::uint64_t>(action.amount.units()));
    fnv1a_u64(hash, action.requested_basis_points);
    fnv1a_u64(hash, static_cast<std::uint64_t>(action.all_in_kind));
  }
  fnv1a_text(hash, accumulator.history_fingerprint);
  fnv1a_u64(hash, static_cast<std::uint64_t>(accumulator.expected_resolver_root_ordinals.size()));
  for (const auto ordinal : accumulator.expected_resolver_root_ordinals) {
    fnv1a_u64(hash, ordinal);
  }
  fnv1a_u64(hash, accumulator.next_boundary_index);
  for (const auto &value : accumulator.values) {
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_reach_sum));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_reach_compensation));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_utility_sum_antes));
    fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_utility_compensation_antes));
  }
  fnv1a_u64(hash, accumulator.complete ? 1U : 0U);
  fnv1a_text(hash, accumulator.contribution_chain_fingerprint);
  return "fnv1a64:" + hex64(hash);
}

std::string upper_terminal_manifest_fingerprint(const std::uint32_t entry_node, const Street street,
                                                const HandStatus status,
                                                const std::string_view history_fingerprint) {
  const auto identity =
      "gtosd.hu_preflop_upper_terminal_manifest.v1|" + std::to_string(entry_node) + "|" +
      std::to_string(static_cast<unsigned>(street)) + "|" +
      std::to_string(static_cast<unsigned>(status)) + "|" + std::string(history_fingerprint);
  return "fnv1a64:" + hex64(fnv1a(identity));
}

std::uint64_t flop_task_numeric_state_hash(const HuPreflopFlopTaskAccumulator &accumulator) {
  auto hash = fnv_offset;
  for (std::size_t resolver = 0U; resolver < accumulator.values.size(); ++resolver) {
    for (std::size_t combo = 0U; combo < accumulator.values[resolver].size(); ++combo) {
      const auto &value = accumulator.values[resolver][combo];
      fnv1a_u64(hash, static_cast<std::uint64_t>(resolver));
      fnv1a_u64(hash, static_cast<std::uint64_t>(combo));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_reach_sum));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_reach_compensation));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_utility_sum_antes));
      fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value.weighted_utility_compensation_antes));
    }
  }
  return hash;
}

std::string flop_task_accumulator_fingerprint(const HuPreflopFlopTaskAccumulator &accumulator) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_flop_task_accumulator.v2|" << accumulator.major << '|'
           << accumulator.minor << '|' << accumulator.tree_fingerprint << '|'
           << accumulator.blueprint_fingerprint << '|' << accumulator.river_aggregate_fingerprint
           << '|' << accumulator.continuation_checkpoint_fingerprint << '|'
           << accumulator.blueprint_iterations << '|' << accumulator.task_span_index << '|'
           << accumulator.entry_node << '|';
  for (const auto card : accumulator.flop) {
    identity << static_cast<unsigned>(card.value()) << ',';
  }
  identity << '|' << accumulator.next_terminal_ordinal << ';' << accumulator.numeric_state_hash
           << '|' << accumulator.complete << '|' << accumulator.contribution_chain_fingerprint
           << '|';
  for (const auto &expected : accumulator.expected_terminal_fingerprints) {
    identity << expected << ';';
  }
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

void compensated_add(const double increment, double &sum, double &compensation) {
  const auto updated = sum + increment;
  compensation += std::abs(sum) >= std::abs(increment) ? (sum - updated) + increment
                                                       : (increment - updated) + sum;
  sum = updated;
}

bool valid_continuation_value_mode(const HuPreflopContinuationValueMode mode) {
  return mode == HuPreflopContinuationValueMode::AverageStrategy ||
         mode == HuPreflopContinuationValueMode::ExactBestResponse;
}

bool valid_accumulator_payload(const HuPreflopRiverTaskAccumulator &accumulator) {
  if (accumulator.major != HuPreflopRiverTaskAccumulator::format_major ||
      accumulator.minor != HuPreflopRiverTaskAccumulator::format_minor ||
      accumulator.tree_fingerprint.empty() || accumulator.blueprint_fingerprint.empty() ||
      accumulator.root_catalog_fingerprint.empty() || accumulator.batch_plan_fingerprint.empty() ||
      accumulator.blueprint_iterations == 0U ||
      !valid_continuation_value_mode(accumulator.value_mode) ||
      accumulator.resolver_root_count == 0U ||
      accumulator.next_resolver_root < accumulator.first_resolver_root ||
      accumulator.next_resolver_root - accumulator.first_resolver_root >
          accumulator.resolver_root_count ||
      accumulator.complete != (accumulator.next_resolver_root - accumulator.first_resolver_root ==
                               accumulator.resolver_root_count) ||
      accumulator.contribution_chain_fingerprint.empty() || !valid_flop(accumulator.flop)) {
    return false;
  }
  const auto accumulated =
      accumulator.accumulated_roots_by_resolver[0] + accumulator.accumulated_roots_by_resolver[1];
  if ((accumulated == 0U) != accumulator.continuation_checkpoint_fingerprint.empty()) {
    return false;
  }
  if (accumulated != accumulator.next_resolver_root - accumulator.first_resolver_root ||
      accumulator.accumulated_roots_by_resolver[0] < accumulator.accumulated_roots_by_resolver[1] ||
      accumulator.accumulated_roots_by_resolver[0] >
          accumulator.accumulated_roots_by_resolver[1] + 1U) {
    return false;
  }
  for (const auto &player : accumulator.values) {
    for (const auto &value : player) {
      if (!std::isfinite(value.weighted_reach_sum) ||
          !std::isfinite(value.weighted_reach_compensation) ||
          !std::isfinite(value.weighted_utility_sum_antes) ||
          !std::isfinite(value.weighted_utility_compensation_antes)) {
        return false;
      }
    }
  }
  return true;
}

bool valid_river_root_catalog(const HuPreflopRiverRootCatalog &catalog) {
  if (catalog.tree_fingerprint.empty() || catalog.decomposition_plan_fingerprint.empty() ||
      catalog.river_work_fingerprint.empty() || catalog.river_shapes.empty() ||
      catalog.canonical_boards.empty() || catalog.task_spans.empty() ||
      catalog.resolver_roots == 0U ||
      catalog.fingerprint != river_root_catalog_fingerprint(catalog)) {
    return false;
  }
  const auto physical_histories =
      std::accumulate(catalog.canonical_boards.begin(), catalog.canonical_boards.end(),
                      std::uint64_t{0}, [](const std::uint64_t sum, const auto &board) {
                        return sum + board.physical_outcome_count;
                      });
  if (physical_histories != catalog.physical_public_board_histories ||
      !std::ranges::all_of(catalog.canonical_boards,
                           [](const auto &board) { return board.physical_outcome_count > 0U; })) {
    return false;
  }
  std::uint64_t expected_first_root = 0U;
  std::set<std::pair<std::uint32_t, std::array<CardId, 3>>> task_keys;
  std::set<std::uint32_t> entry_nodes;
  std::set<std::array<CardId, 3>> flops;
  for (const auto &span : catalog.task_spans) {
    if (span.first_resolver_root != expected_first_root || span.shape_count == 0U ||
        span.board_count == 0U || span.first_shape_index > catalog.river_shapes.size() ||
        span.shape_count > catalog.river_shapes.size() - span.first_shape_index ||
        span.first_board_index > catalog.canonical_boards.size() ||
        span.board_count > catalog.canonical_boards.size() - span.first_board_index ||
        span.flop_catalog_index >= catalog.canonical_boards.size()) {
      return false;
    }
    const auto &flop = catalog.canonical_boards[span.first_board_index].flop;
    if (!task_keys.emplace(span.entry_node, flop).second) {
      return false;
    }
    entry_nodes.insert(span.entry_node);
    flops.insert(flop);
    if (!std::ranges::all_of(
            catalog.river_shapes.begin() + static_cast<std::ptrdiff_t>(span.first_shape_index),
            catalog.river_shapes.begin() +
                static_cast<std::ptrdiff_t>(span.first_shape_index + span.shape_count),
            [&](const auto &shape) { return shape.entry_node == span.entry_node; }) ||
        !std::ranges::all_of(
            catalog.canonical_boards.begin() + static_cast<std::ptrdiff_t>(span.first_board_index),
            catalog.canonical_boards.begin() +
                static_cast<std::ptrdiff_t>(span.first_board_index + span.board_count),
            [&](const auto &board) { return board.flop == flop; })) {
      return false;
    }
    std::uint64_t roots_per_shape = 0U;
    std::uint64_t expected_span_roots = 0U;
    if (!checked_multiply(span.board_count, 2U, roots_per_shape) ||
        !checked_multiply(span.shape_count, roots_per_shape, expected_span_roots) ||
        expected_span_roots != span.resolver_root_count ||
        expected_first_root >
            std::numeric_limits<std::uint64_t>::max() - span.resolver_root_count) {
      return false;
    }
    expected_first_root += span.resolver_root_count;
  }
  std::uint64_t expected_tasks = 0U;
  return checked_multiply(static_cast<std::uint64_t>(entry_nodes.size()),
                          static_cast<std::uint64_t>(flops.size()), expected_tasks) &&
         expected_tasks == catalog.task_spans.size() &&
         expected_first_root == catalog.resolver_roots;
}

Result<HuPreflopRiverResolverRoot, HuPreflopError>
river_resolver_root_at_unchecked(const HuPreflopRiverRootCatalog &catalog,
                                 const HuPreflopRiverBatchPlan &batch_plan,
                                 const std::uint64_t resolver_root_ordinal) {
  const auto upper =
      std::upper_bound(catalog.task_spans.begin(), catalog.task_spans.end(), resolver_root_ordinal,
                       [](const std::uint64_t ordinal, const HuPreflopRiverTaskSpan &span) {
                         return ordinal < span.first_resolver_root;
                       });
  if (upper == catalog.task_spans.begin()) {
    return Result<HuPreflopRiverResolverRoot, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto span_iterator = std::prev(upper);
  const auto task_span_index =
      static_cast<std::uint64_t>(std::distance(catalog.task_spans.begin(), span_iterator));
  const auto &span = *span_iterator;
  const auto local_ordinal = resolver_root_ordinal - span.first_resolver_root;
  std::uint64_t roots_per_shape = 0U;
  if (local_ordinal >= span.resolver_root_count ||
      !checked_multiply(span.board_count, 2U, roots_per_shape) || roots_per_shape == 0U) {
    return Result<HuPreflopRiverResolverRoot, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto shape_index = span.first_shape_index + local_ordinal / roots_per_shape;
  const auto board_ordinal = span.first_board_index + (local_ordinal % roots_per_shape) / 2U;
  const auto resolving_player = static_cast<std::uint8_t>(local_ordinal % 2U);
  if (shape_index >= catalog.river_shapes.size() ||
      board_ordinal >= catalog.canonical_boards.size()) {
    return Result<HuPreflopRiverResolverRoot, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto &shape = catalog.river_shapes[shape_index];
  const auto &board = catalog.canonical_boards[board_ordinal];
  HuPreflopRiverResolverRoot result;
  result.ordinal = resolver_root_ordinal;
  result.task_span_index = task_span_index;
  result.task_first_resolver_root = span.first_resolver_root;
  result.task_resolver_root_count = span.resolver_root_count;
  result.entry_node = shape.entry_node;
  result.flop = board.flop;
  result.turn = board.turn;
  result.river = board.river;
  result.resolving_player = resolving_player;
  result.physical_public_outcome_count = board.physical_outcome_count;
  result.state = shape.state;
  result.state.board_mask = flop_mask(result.flop) | result.turn.mask() | result.river.mask();
  result.action_history = shape.action_history;
  result.history_fingerprint = shape.history_fingerprint;
  result.batch_plan_fingerprint = batch_plan.fingerprint;
  result.fingerprint = river_resolver_root_fingerprint(result);
  return validate_state(result.state)
             ? Result<HuPreflopRiverResolverRoot, HuPreflopError>::success(std::move(result))
             : Result<HuPreflopRiverResolverRoot, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

std::string
river_scheduler_checkpoint_fingerprint(const HuPreflopRiverSchedulerCheckpoint &checkpoint) {
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_river_scheduler_checkpoint.v1|" << checkpoint.major << '|'
           << checkpoint.minor << '|' << checkpoint.tree_fingerprint << '|'
           << checkpoint.blueprint_fingerprint << '|' << checkpoint.batch_plan_fingerprint << '|'
           << checkpoint.completed_batch_count << '|' << checkpoint.completed_resolver_root_count
           << '|' << checkpoint.upper_street_accumulator_fingerprint << '|' << checkpoint.complete;
  return "fnv1a64:" + hex64(fnv1a(identity.str()));
}

bool valid_river_batch_plan(const HuPreflopRiverBatchPlan &plan) {
  if (plan.major != HuPreflopRiverBatchPlan::format_major ||
      plan.minor != HuPreflopRiverBatchPlan::format_minor || plan.tree_fingerprint.empty() ||
      plan.blueprint_fingerprint.empty() || plan.decomposition_plan_fingerprint.empty() ||
      plan.river_work_fingerprint.empty() ||
      plan.resolver_root_ordering !=
          "entry/flop_lex/river_shape_dfs/canonical_ordered_runout/resolver_pair_v3" ||
      plan.resolver_roots == 0U || plan.target_batch_payload_bytes == 0U ||
      plan.boundary_bytes_per_resolver_root == 0U || plan.roots_per_batch == 0U ||
      plan.resolver_roots % 2U != 0U || plan.roots_per_batch % 2U != 0U || plan.batch_count <= 1U ||
      plan.final_batch_root_count == 0U || !plan.reduce_to_upper_street_accumulator ||
      plan.allow_full_boundary_materialization ||
      plan.batch_first_resolver_roots.size() != plan.batch_count ||
      plan.batch_resolver_root_counts.size() != plan.batch_count ||
      plan.batch_task_span_indices.size() != plan.batch_count) {
    return false;
  }
  std::uint64_t expected_first = 0U;
  std::uint64_t maximum_count = 0U;
  for (std::size_t index = 0U; index < plan.batch_count; ++index) {
    const auto count = plan.batch_resolver_root_counts[index];
    if (plan.batch_first_resolver_roots[index] != expected_first || count == 0U ||
        plan.batch_first_resolver_roots[index] % 2U != 0U || count % 2U != 0U ||
        count > plan.roots_per_batch ||
        (index > 0U &&
         plan.batch_task_span_indices[index] < plan.batch_task_span_indices[index - 1U]) ||
        expected_first > std::numeric_limits<std::uint64_t>::max() - count) {
      return false;
    }
    expected_first += count;
    maximum_count = std::max(maximum_count, count);
  }
  std::uint64_t expected_maximum_payload = 0U;
  return expected_first == plan.resolver_roots &&
         plan.final_batch_root_count == plan.batch_resolver_root_counts.back() &&
         checked_multiply(maximum_count, plan.boundary_bytes_per_resolver_root,
                          expected_maximum_payload) &&
         expected_maximum_payload == plan.maximum_batch_payload_bytes &&
         plan.maximum_batch_payload_bytes <= plan.target_batch_payload_bytes &&
         plan.maximum_batch_payload_bytes < plan.fully_materialized_boundary_bytes &&
         plan.fingerprint == river_batch_plan_fingerprint(plan);
}

bool valid_river_batch_plan_for_catalog(const HuPreflopRiverBatchPlan &plan,
                                        const HuPreflopRiverRootCatalog &catalog) {
  if (!valid_river_batch_plan(plan) || !valid_river_root_catalog(catalog) ||
      plan.tree_fingerprint != catalog.tree_fingerprint ||
      plan.decomposition_plan_fingerprint != catalog.decomposition_plan_fingerprint ||
      plan.river_work_fingerprint != catalog.river_work_fingerprint ||
      plan.resolver_roots != catalog.resolver_roots) {
    return false;
  }
  for (std::size_t index = 0U; index < plan.batch_count; ++index) {
    const auto task_index = plan.batch_task_span_indices[index];
    if (task_index >= catalog.task_spans.size()) {
      return false;
    }
    const auto &span = catalog.task_spans[task_index];
    const auto first = plan.batch_first_resolver_roots[index];
    const auto count = plan.batch_resolver_root_counts[index];
    if (first < span.first_resolver_root ||
        first > std::numeric_limits<std::uint64_t>::max() - count ||
        first + count > span.first_resolver_root + span.resolver_root_count) {
      return false;
    }
  }
  return true;
}

bool valid_river_scheduler_checkpoint(const HuPreflopRiverBatchPlan &plan,
                                      const HuPreflopRiverSchedulerCheckpoint &checkpoint) {
  if (!valid_river_batch_plan(plan) ||
      checkpoint.major != HuPreflopRiverSchedulerCheckpoint::format_major ||
      checkpoint.minor != HuPreflopRiverSchedulerCheckpoint::format_minor ||
      checkpoint.tree_fingerprint != plan.tree_fingerprint ||
      checkpoint.blueprint_fingerprint != plan.blueprint_fingerprint ||
      checkpoint.batch_plan_fingerprint != plan.fingerprint ||
      checkpoint.upper_street_accumulator_fingerprint.empty() ||
      checkpoint.completed_batch_count > plan.batch_count ||
      checkpoint.completed_resolver_root_count > plan.resolver_roots ||
      checkpoint.complete != (checkpoint.completed_batch_count == plan.batch_count) ||
      checkpoint.fingerprint != river_scheduler_checkpoint_fingerprint(checkpoint)) {
    return false;
  }
  const auto expected_roots =
      checkpoint.completed_batch_count == 0U
          ? 0U
          : plan.batch_first_resolver_roots[checkpoint.completed_batch_count - 1U] +
                plan.batch_resolver_root_counts[checkpoint.completed_batch_count - 1U];
  return checkpoint.completed_resolver_root_count == expected_roots;
}

struct ClassReachState {
  std::array<std::array<double, hu_preflop_hand_class_count>, 2> players{};
};

Result<ClassReachState, HuPreflopError>
preflop_class_reach_at_node(const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
                            const std::uint32_t target_node) {
  const auto valid_blueprint = validate_hu_preflop_blueprint(tree, blueprint);
  if (!valid_blueprint || target_node >= tree.nodes.size()) {
    return Result<ClassReachState, HuPreflopError>::failure(
        valid_blueprint ? HuPreflopError::InvalidConfiguration : valid_blueprint.error());
  }
  std::map<std::uint32_t, const HuPreflopBlueprintDecision *> decisions;
  for (const auto &decision : blueprint.decisions) {
    decisions.emplace(decision.node_id, &decision);
  }

  ClassReachState initial;
  for (auto &player : initial.players) {
    player.fill(1.0);
  }
  std::vector<std::pair<std::uint32_t, ClassReachState>> pending{{tree.root, initial}};
  std::optional<ClassReachState> result;
  while (!pending.empty()) {
    auto [node_id, reach] = std::move(pending.back());
    pending.pop_back();
    if (node_id >= tree.nodes.size()) {
      return Result<ClassReachState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    if (node_id == target_node) {
      if (result.has_value()) {
        return Result<ClassReachState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      result = std::move(reach);
      continue;
    }
    const auto &node = tree.nodes[node_id];
    if (node.kind != HuPreflopNodeKind::Decision) {
      continue;
    }
    const auto decision = decisions.find(node.id);
    if (decision == decisions.end() || decision->second->action_count != node.edges.size()) {
      return Result<ClassReachState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    for (std::size_t action = 0U; action < node.edges.size(); ++action) {
      auto child_reach = reach;
      for (std::size_t class_id = 0U; class_id < hu_preflop_hand_class_count; ++class_id) {
        child_reach.players[decision->second->player][class_id] *=
            decision->second->strategy[class_id][action];
      }
      pending.emplace_back(node.edges[action].child, std::move(child_reach));
    }
  }
  return result.has_value()
             ? Result<ClassReachState, HuPreflopError>::success(std::move(*result))
             : Result<ClassReachState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

double joint_sequence_probability(const ClassReachState &reach,
                                  const std::array<Combo, 630U> &combos,
                                  const std::array<std::uint64_t, 630U> &masks) {
  double joint_mass = 0.0;
  for (std::size_t first = 0U; first < combos.size(); ++first) {
    const auto first_reach = reach.players[0][hand_class(combos[first])];
    if (first_reach == 0.0) {
      continue;
    }
    for (std::size_t second = 0U; second < combos.size(); ++second) {
      if ((masks[first] & masks[second]) == 0U) {
        joint_mass += first_reach * reach.players[1][hand_class(combos[second])];
      }
    }
  }
  return joint_mass / static_cast<double>(physical_private_deals);
}

Result<HuPreflopConditionedRanges, HuPreflopError>
condition_ranges_unchecked(const HuPreflopPostflopEntryReach &entry,
                           const std::array<CardId, 3> &flop) {
  HuPreflopConditionedRanges conditioned;
  conditioned.entry_node = entry.entry_node;
  conditioned.flop = flop;
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(flop);
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    if ((masks[combo] & board_mask) != 0U) {
      continue;
    }
    for (std::size_t player = 0U; player < 2U; ++player) {
      conditioned.exact_relative_reach[player][combo] = entry.own_sequence_reach[player][combo];
      conditioned.live_positive_combo_count[player] +=
          entry.own_sequence_reach[player][combo] > 0.0 ? 1U : 0U;
    }
  }
  const auto combos = all_combos();
  conditioned.compatible_joint_reach_mass = compatible_joint_mass(
      combos, masks, entry.own_sequence_reach[0], entry.own_sequence_reach[1], board_mask);
  if (conditioned.compatible_joint_reach_mass <= 0.0 ||
      !std::isfinite(conditioned.compatible_joint_reach_mass)) {
    return Result<HuPreflopConditionedRanges, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  return Result<HuPreflopConditionedRanges, HuPreflopError>::success(std::move(conditioned));
}

bool valid_whole_game_plan_contract(const HuPreflopTree &tree,
                                    const HuPreflopDecompositionPlan &plan,
                                    const double target_normalized_nashconv) {
  std::uint64_t expected_physical_roots = 0U;
  std::uint64_t expected_canonical_roots = 0U;
  if (!checked_multiply(tree.stats.postflop_entries, physical_flops, expected_physical_roots) ||
      !checked_multiply(tree.stats.postflop_entries, plan.canonical_flops,
                        expected_canonical_roots)) {
    return false;
  }
  const auto physical_catalog_mass =
      std::accumulate(plan.canonical_flop_catalog.begin(), plan.canonical_flop_catalog.end(),
                      std::uint64_t{0}, [](const std::uint64_t total, const auto &flop) {
                        return total + flop.physical_outcome_count;
                      });
  return tree.fingerprint == plan.tree_fingerprint &&
         plan.major == HuPreflopDecompositionPlan::format_major &&
         plan.minor == HuPreflopDecompositionPlan::format_minor && !plan.fingerprint.empty() &&
         plan.fingerprint == plan_fingerprint(plan) &&
         plan.entries.size() == tree.stats.postflop_entries &&
         plan.physical_private_deals == physical_private_deals &&
         plan.physical_flops == physical_flops && plan.canonical_flops == 573U &&
         plan.canonical_flop_catalog.size() == plan.canonical_flops &&
         plan.public_flop_roots == expected_physical_roots &&
         plan.canonical_public_flop_roots == expected_canonical_roots &&
         physical_catalog_mass == physical_flops && std::isfinite(plan.total_probability) &&
         std::abs(plan.total_probability - 1.0) <= 1.0e-10 && target_normalized_nashconv >= 0.0 &&
         std::isfinite(target_normalized_nashconv);
}

std::size_t whole_game_task_index(const HuPreflopDecompositionPlan &plan,
                                  const std::uint32_t entry_node,
                                  const std::array<CardId, 3> &flop) {
  const auto entry =
      std::ranges::find(plan.entries, entry_node, &HuPreflopPostflopEntryReach::entry_node);
  const auto catalog =
      std::ranges::find(plan.canonical_flop_catalog, flop, &HuPreflopCanonicalFlop::cards);
  if (entry == plan.entries.end() || catalog == plan.canonical_flop_catalog.end()) {
    return std::numeric_limits<std::size_t>::max();
  }
  const auto entry_index = static_cast<std::size_t>(entry - plan.entries.begin());
  const auto flop_index = static_cast<std::size_t>(catalog - plan.canonical_flop_catalog.begin());
  if (entry_index > std::numeric_limits<std::size_t>::max() / plan.canonical_flop_catalog.size()) {
    return std::numeric_limits<std::size_t>::max();
  }
  return entry_index * plan.canonical_flop_catalog.size() + flop_index;
}

Result<HuPreflopCanonicalFlopTask, HuPreflopError>
canonical_flop_task_at(const HuPreflopDecompositionPlan &plan, const std::size_t task_index) {
  if (plan.entries.empty() || plan.canonical_flop_catalog.empty() ||
      plan.canonical_flops != plan.canonical_flop_catalog.size() ||
      task_index >= plan.canonical_public_flop_roots) {
    return Result<HuPreflopCanonicalFlopTask, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto flop_count = plan.canonical_flop_catalog.size();
  const auto entry_index = task_index / flop_count;
  const auto flop_index = task_index % flop_count;
  if (entry_index >= plan.entries.size()) {
    return Result<HuPreflopCanonicalFlopTask, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto &entry = plan.entries[entry_index];
  const auto &flop = plan.canonical_flop_catalog[flop_index];
  const auto conditioned = condition_ranges_unchecked(entry, flop.cards);
  if (!conditioned) {
    return Result<HuPreflopCanonicalFlopTask, HuPreflopError>::failure(conditioned.error());
  }
  const auto probability =
      static_cast<double>(flop.physical_outcome_count) *
      conditioned.value().compatible_joint_reach_mass /
      (static_cast<double>(physical_private_deals) * static_cast<double>(flops_per_private_deal));
  if (!std::isfinite(probability) || probability < 0.0) {
    return Result<HuPreflopCanonicalFlopTask, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  std::ostringstream identity;
  identity << "gtosd.hu_preflop_flop_task.v1|" << plan.fingerprint << '|' << entry.entry_node
           << '|';
  for (const auto card : flop.cards) {
    identity << static_cast<unsigned>(card.value()) << ',';
  }
  identity << flop.physical_outcome_count << '|' << std::setprecision(17) << probability;
  return Result<HuPreflopCanonicalFlopTask, HuPreflopError>::success(
      HuPreflopCanonicalFlopTask{entry.entry_node, flop.cards, flop.physical_outcome_count,
                                 probability, "fnv1a64:" + hex64(fnv1a(identity.str()))});
}

} // namespace

std::string fingerprint_hu_preflop_decomposition_plan(const HuPreflopDecompositionPlan &plan) {
  return plan_fingerprint(plan);
}

std::string fingerprint_hu_preflop_flop_boundary(const HuPreflopFlopBoundary &boundary) {
  return boundary_fingerprint(boundary);
}

std::string
fingerprint_hu_preflop_global_best_response(const HuPreflopGlobalBestResponseEvidence &evidence) {
  std::ostringstream serialized;
  serialized << "gtosd.hu_preflop_global_best_response.v1|" << evidence.major << '|'
             << evidence.minor << '|' << evidence.tree_fingerprint << '|'
             << evidence.blueprint_fingerprint << '|' << evidence.continuation_profile_fingerprint
             << '|' << evidence.profile_evaluation_fingerprint << '|' << evidence.method << '|'
             << evidence.blueprint_iterations << '|' << evidence.chance_outcome_count << '|'
             << std::setprecision(std::numeric_limits<double>::max_digits10)
             << evidence.best_response_values_antes[0] << '|'
             << evidence.best_response_values_antes[1] << '|' << evidence.profile_values_antes[0]
             << '|' << evidence.profile_values_antes[1] << '|' << evidence.deviation_gains_antes[0]
             << '|' << evidence.deviation_gains_antes[1] << '|' << evidence.nashconv_antes << '|'
             << evidence.normalized_nashconv << '|' << evidence.exact_best_response;
  return "fnv1a64:" + hex64(fnv1a(serialized.str()));
}

std::string fingerprint_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopWholeGameCoverageAccumulator &accumulator) {
  std::ostringstream serialized;
  serialized << "gtosd.hu_preflop_whole_game_coverage.v1|" << accumulator.major << '|'
             << accumulator.minor << '|' << accumulator.tree_fingerprint << '|'
             << accumulator.blueprint_fingerprint << '|' << accumulator.plan_fingerprint << '|'
             << std::setprecision(std::numeric_limits<double>::max_digits10)
             << accumulator.target_normalized_nashconv << '|' << accumulator.blueprint_iterations
             << '|' << accumulator.continuation_checkpoint_fingerprint << '|'
             << accumulator.validated_boundary_count << '|' << accumulator.fully_covered_task_count
             << '|' << accumulator.covered_postflop_probability << '|'
             << accumulator.coverage_state_hash << '|' << accumulator.continuation_profile_hash
             << '|' << accumulator.profile_utility_state_hash << '|'
             << accumulator.local_continuation_identity_state_hash << '|'
             << accumulator.contribution_chain_fingerprint << '|'
             << accumulator.task_side_coverage.size() << '|'
             << accumulator.task_joint_probabilities.size() << '|'
             << accumulator.task_side_profile_utility_antes.size() << '|'
             << accumulator.task_local_continuation_identity_hashes.size();
  return "fnv1a64:" + hex64(fnv1a(serialized.str()));
}

std::string fingerprint_hu_preflop_continuation_profile(
    const HuPreflopWholeGameCoverageAccumulator &accumulator) {
  std::ostringstream serialized;
  serialized << "gtosd.hu_preflop_continuation_profile.v1|" << accumulator.tree_fingerprint << '|'
             << accumulator.blueprint_fingerprint << '|' << accumulator.plan_fingerprint << '|'
             << accumulator.continuation_checkpoint_fingerprint << '|'
             << accumulator.task_side_coverage.size() << '|' << accumulator.validated_boundary_count
             << '|' << accumulator.coverage_state_hash << '|'
             << accumulator.continuation_profile_hash << '|'
             << accumulator.local_continuation_identity_state_hash;
  return "fnv1a64:" + hex64(fnv1a(serialized.str()));
}

std::string fingerprint_hu_preflop_river_batch_plan(const HuPreflopRiverBatchPlan &plan) {
  return river_batch_plan_fingerprint(plan);
}

std::string fingerprint_hu_preflop_river_batch(const HuPreflopRiverBatch &batch) {
  return river_batch_fingerprint(batch);
}

std::string fingerprint_hu_preflop_river_resolver_root(const HuPreflopRiverResolverRoot &root) {
  return river_resolver_root_fingerprint(root);
}

std::string fingerprint_hu_preflop_river_root_catalog(const HuPreflopRiverRootCatalog &catalog) {
  return river_root_catalog_fingerprint(catalog);
}

std::string fingerprint_hu_preflop_river_root_boundary(const HuPreflopRiverRootBoundary &boundary) {
  return river_root_boundary_fingerprint(boundary);
}

std::string
fingerprint_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator) {
  return river_task_accumulator_fingerprint(accumulator);
}

std::string
fingerprint_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate) {
  return river_task_aggregate_fingerprint(aggregate);
}

std::string fingerprint_hu_preflop_upper_street_best_response_terminal_contribution(
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution) {
  return upper_best_response_terminal_contribution_fingerprint(contribution);
}

std::string fingerprint_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopUpperStreetTerminalContribution &contribution) {
  return upper_terminal_contribution_fingerprint(contribution);
}

std::string
fingerprint_hu_preflop_flop_task_accumulator(const HuPreflopFlopTaskAccumulator &accumulator) {
  return flop_task_accumulator_fingerprint(accumulator);
}

std::string fingerprint_hu_preflop_river_scheduler_checkpoint(
    const HuPreflopRiverSchedulerCheckpoint &checkpoint) {
  return river_scheduler_checkpoint_fingerprint(checkpoint);
}

Result<HuPreflopDecompositionPlan, HuPreflopError>
derive_hu_preflop_decomposition_plan(const HuPreflopTree &tree,
                                     const HuPreflopBlueprint &blueprint) {
  const auto valid = validate_hu_preflop_blueprint(tree, blueprint);
  if (!valid) {
    return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(valid.error());
  }
  std::map<std::uint32_t, const HuPreflopBlueprintDecision *> decisions;
  for (const auto &decision : blueprint.decisions) {
    decisions.emplace(decision.node_id, &decision);
  }

  ClassReachState initial;
  for (auto &player : initial.players) {
    player.fill(1.0);
  }
  std::vector<std::pair<std::uint32_t, ClassReachState>> pending{{tree.root, initial}};
  HuPreflopDecompositionPlan plan;
  plan.tree_fingerprint = tree.fingerprint;
  plan.blueprint_fingerprint = blueprint.fingerprint;
  const auto combos = all_combos();
  const auto masks = combo_masks();
  while (!pending.empty()) {
    auto [node_id, reach] = std::move(pending.back());
    pending.pop_back();
    if (node_id >= tree.nodes.size()) {
      return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
          HuPreflopError::GameFailure);
    }
    const auto &node = tree.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      HuPreflopPostflopEntryReach entry;
      entry.entry_node = node.id;
      for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
        const auto class_id = hand_class(combos[combo]);
        for (std::size_t player = 0U; player < 2U; ++player) {
          entry.own_sequence_reach[player][combo] = reach.players[player][class_id];
          entry.positive_combo_count[player] += reach.players[player][class_id] > 0.0 ? 1U : 0U;
        }
      }
      const auto joint_mass = compatible_joint_mass(combos, masks, entry.own_sequence_reach[0],
                                                    entry.own_sequence_reach[1]);
      entry.joint_entry_probability = joint_mass / static_cast<double>(physical_private_deals);
      plan.postflop_entry_probability += entry.joint_entry_probability;
      plan.entries.push_back(std::move(entry));
      continue;
    }
    if (node.kind == HuPreflopNodeKind::TerminalFold) {
      plan.terminal_fold_probability += joint_sequence_probability(reach, combos, masks);
      continue;
    }
    if (node.kind == HuPreflopNodeKind::TerminalAllIn) {
      plan.terminal_all_in_probability += joint_sequence_probability(reach, combos, masks);
      continue;
    }
    if (node.kind != HuPreflopNodeKind::Decision) {
      continue;
    }
    const auto decision = decisions.at(node.id);
    for (std::size_t action = 0U; action < node.edges.size(); ++action) {
      auto child_reach = reach;
      for (std::size_t class_id = 0U; class_id < hu_preflop_hand_class_count; ++class_id) {
        child_reach.players[decision->player][class_id] *= decision->strategy[class_id][action];
      }
      pending.emplace_back(node.edges[action].child, std::move(child_reach));
    }
  }
  std::ranges::sort(plan.entries, {}, &HuPreflopPostflopEntryReach::entry_node);
  if (plan.entries.size() != tree.stats.postflop_entries) {
    return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }

  plan.physical_private_deals = physical_private_deals;
  plan.physical_flops = physical_flops;
  plan.canonical_flop_catalog = canonical_flop_catalog();
  plan.canonical_flops = plan.canonical_flop_catalog.size();
  plan.maximum_live_combos_per_player = live_combos_per_flop;
  plan.maximum_compatible_deals_per_flop = compatible_deals_per_flop;
  plan.total_probability = plan.postflop_entry_probability + plan.terminal_fold_probability +
                           plan.terminal_all_in_probability;
  if (!std::isfinite(plan.total_probability) || std::abs(plan.total_probability - 1.0) > 1.0e-10) {
    return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  const auto entries = static_cast<std::uint64_t>(plan.entries.size());
  std::uint64_t streaming_ledger_snapshots_bytes = 0U;
  std::uint64_t streaming_boundary_batch_bytes = 0U;
  if (!checked_multiply(entries, physical_flops, plan.public_flop_roots) ||
      !checked_multiply(entries, plan.canonical_flops, plan.canonical_public_flop_roots) ||
      !checked_multiply(entries, physical_private_deals * flops_per_private_deal,
                        plan.physical_deal_flop_histories) ||
      !checked_multiply(entries, 2U * 630U * sizeof(double), plan.bytes.reach_template_bytes) ||
      !checked_multiply(2U * 630U, sizeof(double), plan.bytes.one_flop_conditioned_range_bytes) ||
      !checked_multiply(live_combos_per_flop, dense_boundary_record_bytes,
                        plan.bytes.one_resolver_boundary_bytes) ||
      !checked_multiply(plan.public_flop_roots, plan.bytes.one_resolver_boundary_bytes,
                        plan.bytes.all_resolver_boundaries_bytes) ||
      !checked_multiply(plan.bytes.all_resolver_boundaries_bytes, 2U,
                        plan.bytes.both_players_boundary_bytes) ||
      !checked_multiply(plan.canonical_public_flop_roots, plan.bytes.one_resolver_boundary_bytes,
                        plan.bytes.canonical_all_resolver_boundaries_bytes) ||
      !checked_multiply(plan.bytes.canonical_all_resolver_boundaries_bytes, 2U,
                        plan.bytes.canonical_both_players_boundary_bytes) ||
      !checked_multiply(plan.public_flop_roots, plan.bytes.one_flop_conditioned_range_bytes,
                        plan.bytes.fully_materialized_range_bytes) ||
      !checked_multiply(plan.canonical_public_flop_roots,
                        plan.bytes.one_flop_conditioned_range_bytes,
                        plan.bytes.canonical_fully_materialized_range_bytes) ||
      !checked_multiply(plan.canonical_public_flop_roots, sizeof(std::uint8_t),
                        plan.bytes.whole_game_coverage_mask_bytes) ||
      !checked_multiply(plan.canonical_public_flop_roots, sizeof(double),
                        plan.bytes.whole_game_coverage_probability_bytes) ||
      !checked_multiply(plan.canonical_public_flop_roots, 2U * sizeof(double),
                        plan.bytes.whole_game_profile_utility_bytes) ||
      !checked_multiply(plan.canonical_public_flop_roots, sizeof(std::uint64_t),
                        plan.bytes.whole_game_local_continuation_identity_bytes) ||
      !checked_add(plan.bytes.whole_game_coverage_mask_bytes,
                   plan.bytes.whole_game_coverage_probability_bytes,
                   plan.bytes.whole_game_coverage_payload_bytes) ||
      !checked_add(plan.bytes.whole_game_coverage_payload_bytes,
                   plan.bytes.whole_game_profile_utility_bytes,
                   plan.bytes.whole_game_coverage_payload_bytes) ||
      !checked_add(plan.bytes.whole_game_coverage_payload_bytes,
                   plan.bytes.whole_game_local_continuation_identity_bytes,
                   plan.bytes.whole_game_coverage_payload_bytes) ||
      !checked_multiply(plan.bytes.whole_game_coverage_payload_bytes, 2U,
                        streaming_ledger_snapshots_bytes) ||
      !checked_multiply(plan.bytes.one_resolver_boundary_bytes, 2U,
                        streaming_boundary_batch_bytes) ||
      !checked_add(streaming_ledger_snapshots_bytes, streaming_boundary_batch_bytes,
                   plan.bytes.streaming_certification_live_payload_bytes)) {
    return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  plan.fingerprint = plan_fingerprint(plan);
  return Result<HuPreflopDecompositionPlan, HuPreflopError>::success(std::move(plan));
}

Result<HuPreflopConditionedRanges, HuPreflopError>
condition_hu_preflop_ranges_on_flop(const HuPreflopDecompositionPlan &plan,
                                    const std::uint32_t entry_node,
                                    const std::array<CardId, 3> &requested_flop) {
  const auto flop = canonical_flop(requested_flop);
  const auto *entry = find_entry(plan, entry_node);
  if (plan.major != HuPreflopDecompositionPlan::format_major ||
      plan.minor != HuPreflopDecompositionPlan::format_minor || plan.fingerprint.empty() ||
      plan.fingerprint != plan_fingerprint(plan) || entry == nullptr || !valid_flop(flop)) {
    return Result<HuPreflopConditionedRanges, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  return condition_ranges_unchecked(*entry, flop);
}

Result<std::vector<HuPreflopCanonicalFlopTask>, HuPreflopError>
enumerate_hu_preflop_canonical_flop_tasks(const HuPreflopDecompositionPlan &plan) {
  if (plan.major != HuPreflopDecompositionPlan::format_major ||
      plan.minor != HuPreflopDecompositionPlan::format_minor || plan.fingerprint.empty() ||
      plan.fingerprint != plan_fingerprint(plan) || plan.entries.empty() ||
      plan.canonical_flop_catalog.empty()) {
    return Result<std::vector<HuPreflopCanonicalFlopTask>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  std::vector<HuPreflopCanonicalFlopTask> tasks;
  tasks.reserve(static_cast<std::size_t>(plan.canonical_public_flop_roots));
  double total_probability = 0.0;
  for (std::size_t task_index = 0U;
       task_index < static_cast<std::size_t>(plan.canonical_public_flop_roots); ++task_index) {
    auto task = canonical_flop_task_at(plan, task_index);
    if (!task) {
      return Result<std::vector<HuPreflopCanonicalFlopTask>, HuPreflopError>::failure(task.error());
    }
    total_probability += task.value().joint_probability;
    tasks.push_back(std::move(task.value()));
  }
  if (tasks.size() != plan.canonical_public_flop_roots || !std::isfinite(total_probability) ||
      std::abs(total_probability - plan.postflop_entry_probability) > 1.0e-10) {
    return Result<std::vector<HuPreflopCanonicalFlopTask>, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  return Result<std::vector<HuPreflopCanonicalFlopTask>, HuPreflopError>::success(std::move(tasks));
}

Result<HuPreflopQuantizedRanges, HuPreflopError>
quantize_hu_preflop_conditioned_ranges(const HuPreflopConditionedRanges &conditioned) {
  HuPreflopQuantizedRanges result;
  for (std::size_t player = 0U; player < 2U; ++player) {
    const auto maximum = *std::ranges::max_element(conditioned.exact_relative_reach[player]);
    if (!std::isfinite(maximum) || maximum <= 0.0) {
      return Result<HuPreflopQuantizedRanges, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    for (std::size_t combo = 0U; combo < 630U; ++combo) {
      const auto normalized = conditioned.exact_relative_reach[player][combo] / maximum;
      const auto rounded = static_cast<std::int64_t>(std::llround(normalized * 10'000.0));
      const auto weight = RangeWeight::from_basis_points(rounded);
      if (!weight) {
        return Result<HuPreflopQuantizedRanges, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      result.ranges.players[player][combo] = weight.value();
      const auto restored = static_cast<double>(weight.value().basis_points()) / 10'000.0;
      const auto error = std::abs(restored - normalized);
      result.maximum_absolute_probability_error =
          std::max(result.maximum_absolute_probability_error, error);
      result.total_absolute_probability_error += error;
    }
  }
  return Result<HuPreflopQuantizedRanges, HuPreflopError>::success(std::move(result));
}

Result<HuPreflopFlopBoundary, HuPreflopError> build_hu_preflop_flop_boundary(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopConditionedRanges &conditioned, const std::uint8_t resolving_player,
    const HuPreflopComboReach &opponent_blueprint_cfv_antes, std::string continuation_fingerprint,
    const std::uint64_t blueprint_iterations, std::string continuation_checkpoint_fingerprint) {
  if (continuation_checkpoint_fingerprint.empty()) {
    continuation_checkpoint_fingerprint = continuation_fingerprint;
  }
  const auto *entry = find_entry(plan, conditioned.entry_node);
  if (entry == nullptr || resolving_player > 1U || continuation_fingerprint.empty() ||
      continuation_checkpoint_fingerprint.empty() || blueprint_iterations == 0U ||
      conditioned.flop != canonical_flop(conditioned.flop) || !valid_flop(conditioned.flop) ||
      plan.tree_fingerprint != tree.fingerprint) {
    return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto expected_conditioned =
      condition_hu_preflop_ranges_on_flop(plan, conditioned.entry_node, conditioned.flop);
  if (!expected_conditioned ||
      expected_conditioned.value().exact_relative_reach != conditioned.exact_relative_reach ||
      expected_conditioned.value().live_positive_combo_count !=
          conditioned.live_positive_combo_count ||
      expected_conditioned.value().compatible_joint_reach_mass !=
          conditioned.compatible_joint_reach_mass) {
    return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto opponent = static_cast<std::uint8_t>(1U - resolving_player);
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(conditioned.flop);
  const double chance_denominator =
      static_cast<double>(physical_private_deals) * static_cast<double>(flops_per_private_deal);
  HuPreflopFlopBoundary boundary;
  boundary.tree_fingerprint = tree.fingerprint;
  boundary.blueprint_fingerprint = plan.blueprint_fingerprint;
  boundary.continuation_fingerprint = std::move(continuation_fingerprint);
  boundary.continuation_checkpoint_fingerprint = std::move(continuation_checkpoint_fingerprint);
  boundary.blueprint_iterations = blueprint_iterations;
  boundary.entry_node = conditioned.entry_node;
  boundary.flop = conditioned.flop;
  boundary.resolving_player = resolving_player;
  boundary.opponent = opponent;
  boundary.values.reserve(live_combos_per_flop);
  for (std::size_t opponent_combo = 0U; opponent_combo < masks.size(); ++opponent_combo) {
    if ((masks[opponent_combo] & board_mask) != 0U) {
      continue;
    }
    double counterfactual_reach = 0.0;
    for (std::size_t resolving_combo = 0U; resolving_combo < masks.size(); ++resolving_combo) {
      if ((masks[resolving_combo] & board_mask) == 0U &&
          (masks[resolving_combo] & masks[opponent_combo]) == 0U) {
        counterfactual_reach += entry->own_sequence_reach[resolving_player][resolving_combo];
      }
    }
    counterfactual_reach /= chance_denominator;
    const bool positive = counterfactual_reach > 0.0;
    const auto cfv = positive ? opponent_blueprint_cfv_antes[opponent_combo] : 0.0;
    if (!std::isfinite(cfv)) {
      return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    boundary.values.push_back(
        {static_cast<ComboId>(opponent_combo), counterfactual_reach, cfv, positive});
  }
  boundary.fingerprint = boundary_fingerprint(boundary);
  const auto valid = validate_hu_preflop_flop_boundary(tree, plan, boundary);
  return valid ? Result<HuPreflopFlopBoundary, HuPreflopError>::success(std::move(boundary))
               : Result<HuPreflopFlopBoundary, HuPreflopError>::failure(valid.error());
}

Result<bool, HuPreflopError>
validate_hu_preflop_flop_boundary(const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
                                  const HuPreflopFlopBoundary &boundary) {
  const auto *entry = find_entry(plan, boundary.entry_node);
  if (entry == nullptr || boundary.major != HuPreflopFlopBoundary::format_major ||
      boundary.minor != HuPreflopFlopBoundary::format_minor ||
      boundary.tree_fingerprint != tree.fingerprint ||
      boundary.blueprint_fingerprint != plan.blueprint_fingerprint ||
      boundary.continuation_fingerprint.empty() ||
      boundary.continuation_checkpoint_fingerprint.empty() || boundary.blueprint_iterations == 0U ||
      boundary.resolving_player > 1U || boundary.opponent != 1U - boundary.resolving_player ||
      boundary.flop != canonical_flop(boundary.flop) || !valid_flop(boundary.flop) ||
      boundary.values.size() != live_combos_per_flop || boundary.fingerprint.empty() ||
      boundary.fingerprint != boundary_fingerprint(boundary)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(boundary.flop);
  const double chance_denominator =
      static_cast<double>(physical_private_deals) * static_cast<double>(flops_per_private_deal);
  const auto combos = all_combos();
  std::array<double, 36U> resolving_mass_by_card{};
  double resolving_total = 0.0;
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    if ((masks[combo] & board_mask) != 0U) {
      continue;
    }
    const auto reach = entry->own_sequence_reach[boundary.resolving_player][combo];
    const auto physical_combo = combos[combo];
    resolving_total += reach;
    resolving_mass_by_card[physical_combo.first.value()] += reach;
    resolving_mass_by_card[physical_combo.second.value()] += reach;
  }
  std::array<bool, 630U> observed{};
  bool has_positive = false;
  for (const auto &value : boundary.values) {
    if (value.opponent_combo >= masks.size() || observed[value.opponent_combo] ||
        (masks[value.opponent_combo] & board_mask) != 0U ||
        !std::isfinite(value.counterfactual_reach) || value.counterfactual_reach < 0.0 ||
        !std::isfinite(value.blueprint_counterfactual_value_antes) ||
        value.positive_reach != (value.counterfactual_reach > 0.0) ||
        (!value.positive_reach && value.blueprint_counterfactual_value_antes != 0.0)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
    const auto opponent_combo = combos[value.opponent_combo];
    double expected_reach =
        resolving_total - resolving_mass_by_card[opponent_combo.first.value()] -
        resolving_mass_by_card[opponent_combo.second.value()] +
        entry->own_sequence_reach[boundary.resolving_player][value.opponent_combo];
    expected_reach /= chance_denominator;
    if (std::abs(value.counterfactual_reach - expected_reach) >
        probability_tolerance * std::max(1.0, std::abs(expected_reach))) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
    observed[value.opponent_combo] = true;
    has_positive = has_positive || value.positive_reach;
  }
  return has_positive ? Result<bool, HuPreflopError>::success(true)
                      : Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
}

Result<HuPreflopWholeGameCertification, HuPreflopError>
finalize_hu_preflop_whole_game_certification(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopWholeGameCoverageAccumulator &accumulator,
    const HuPreflopGlobalBestResponseEvidence *global_best_response) {
  const auto valid = validate_hu_preflop_whole_game_coverage_accumulator(tree, plan, accumulator);
  if (!valid) {
    return Result<HuPreflopWholeGameCertification, HuPreflopError>::failure(valid.error());
  }

  HuPreflopWholeGameCertification result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = plan.blueprint_fingerprint;
  result.plan_fingerprint = plan.fingerprint;
  result.expected_boundary_count =
      static_cast<std::uint64_t>(accumulator.task_side_coverage.size()) * 2U;
  result.validated_boundary_count = accumulator.validated_boundary_count;
  result.fully_covered_task_count = accumulator.fully_covered_task_count;
  result.missing_boundary_count = result.expected_boundary_count - result.validated_boundary_count;
  result.covered_postflop_probability = accumulator.covered_postflop_probability;
  result.expected_postflop_probability = plan.postflop_entry_probability;
  result.target_normalized_nashconv = accumulator.target_normalized_nashconv;
  result.continuation_profile_fingerprint =
      fingerprint_hu_preflop_continuation_profile(accumulator);
  result.boundary_coverage_complete =
      result.missing_boundary_count == 0U &&
      result.fully_covered_task_count == accumulator.task_side_coverage.size() &&
      std::abs(result.covered_postflop_probability - result.expected_postflop_probability) <=
          probability_tolerance;

  if (global_best_response != nullptr) {
    const auto &evidence = *global_best_response;
    const auto stack_antes = static_cast<double>(tree.config.effective_stack.units()) /
                             static_cast<double>(Money::units_per_ante);
    const auto normalized = evidence.nashconv_antes / stack_antes;
    bool exact_payload_valid = true;
    if (evidence.exact_best_response) {
      std::uint64_t expected_chance_outcomes = 0U;
      exact_payload_valid =
          checked_multiply(plan.physical_private_deals, ordered_public_runouts_per_private_deal,
                           expected_chance_outcomes) &&
          !evidence.profile_evaluation_fingerprint.empty() && evidence.blueprint_iterations > 0U &&
          evidence.chance_outcome_count == expected_chance_outcomes;
      double reconstructed_nashconv = 0.0;
      for (std::size_t player = 0U; exact_payload_valid && player < 2U; ++player) {
        const auto best_response = evidence.best_response_values_antes[player];
        const auto profile = evidence.profile_values_antes[player];
        const auto gain = evidence.deviation_gains_antes[player];
        const auto expected_gain = best_response - profile;
        const auto tolerance =
            1.0e-12 * std::max({1.0, std::abs(best_response), std::abs(profile), std::abs(gain)});
        exact_payload_valid = std::isfinite(best_response) && std::isfinite(profile) &&
                              std::isfinite(gain) && gain >= 0.0 && expected_gain >= -tolerance &&
                              std::abs(gain - std::max(0.0, expected_gain)) <= tolerance;
        reconstructed_nashconv += gain;
      }
      const auto nashconv_tolerance = 1.0e-12 * std::max({1.0, std::abs(reconstructed_nashconv),
                                                          std::abs(evidence.nashconv_antes)});
      exact_payload_valid =
          exact_payload_valid &&
          std::abs(reconstructed_nashconv - evidence.nashconv_antes) <= nashconv_tolerance;
    }
    if (evidence.major != HuPreflopGlobalBestResponseEvidence::format_major ||
        evidence.minor != HuPreflopGlobalBestResponseEvidence::format_minor ||
        evidence.tree_fingerprint != tree.fingerprint ||
        evidence.blueprint_fingerprint != plan.blueprint_fingerprint ||
        evidence.continuation_profile_fingerprint != result.continuation_profile_fingerprint ||
        evidence.method.empty() || !std::isfinite(evidence.nashconv_antes) ||
        evidence.nashconv_antes < 0.0 || !std::isfinite(evidence.normalized_nashconv) ||
        evidence.normalized_nashconv < 0.0 ||
        std::abs(evidence.normalized_nashconv - normalized) > 1.0e-12 || !exact_payload_valid ||
        evidence.fingerprint != fingerprint_hu_preflop_global_best_response(evidence)) {
      return Result<HuPreflopWholeGameCertification, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.global_best_response_present = true;
    result.global_best_response_exact = evidence.exact_best_response;
    result.normalized_nashconv = evidence.normalized_nashconv;
  }

  result.certified = result.boundary_coverage_complete && result.global_best_response_present &&
                     result.global_best_response_exact &&
                     result.normalized_nashconv <= result.target_normalized_nashconv;
  if (result.certified) {
    result.status = "CERTIFIED";
  } else if (!result.boundary_coverage_complete) {
    result.status = "INCOMPLETE_BOUNDARY_COVERAGE";
  } else if (!result.global_best_response_present) {
    result.status = "GLOBAL_BEST_RESPONSE_MISSING";
  } else if (!result.global_best_response_exact) {
    result.status = "GLOBAL_BEST_RESPONSE_NOT_EXACT";
  } else {
    result.status = "NASHCONV_TARGET_NOT_MET";
  }
  return Result<HuPreflopWholeGameCertification, HuPreflopError>::success(std::move(result));
}

Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>
make_hu_preflop_whole_game_coverage_accumulator(const HuPreflopTree &tree,
                                                const HuPreflopDecompositionPlan &plan,
                                                const double target_normalized_nashconv) {
  if (!valid_whole_game_plan_contract(tree, plan, target_normalized_nashconv)) {
    return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  if (plan.canonical_public_flop_roots > std::numeric_limits<std::size_t>::max() ||
      plan.canonical_public_flop_roots > std::numeric_limits<std::uint64_t>::max() / 2U) {
    return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  const auto task_count = static_cast<std::size_t>(plan.canonical_public_flop_roots);

  HuPreflopWholeGameCoverageAccumulator result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = plan.blueprint_fingerprint;
  result.plan_fingerprint = plan.fingerprint;
  result.target_normalized_nashconv = target_normalized_nashconv;
  result.task_side_coverage.assign(task_count, 0U);
  result.task_side_profile_utility_antes.assign(task_count * 2U, 0.0);
  result.task_local_continuation_identity_hashes.assign(task_count, 0U);
  result.task_joint_probabilities.reserve(task_count);
  for (std::size_t task_index = 0U; task_index < task_count; ++task_index) {
    const auto task = canonical_flop_task_at(plan, task_index);
    if (!task) {
      return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(task.error());
    }
    result.task_joint_probabilities.push_back(task.value().joint_probability);
  }
  result.coverage_state_hash = whole_game_coverage_state_hash(result.task_side_coverage);
  result.profile_utility_state_hash =
      whole_game_profile_utility_state_hash(result.task_side_profile_utility_antes);
  result.local_continuation_identity_state_hash =
      whole_game_local_continuation_state_hash(result.task_local_continuation_identity_hashes);
  result.contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a("gtosd.hu_preflop_whole_game_coverage_contributions.v1|" + plan.fingerprint));
  result.fingerprint = fingerprint_hu_preflop_whole_game_coverage_accumulator(result);
  return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::success(std::move(result));
}

Result<bool, HuPreflopError> validate_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopWholeGameCoverageAccumulator &accumulator) {
  if (accumulator.major != HuPreflopWholeGameCoverageAccumulator::format_major ||
      accumulator.minor != HuPreflopWholeGameCoverageAccumulator::format_minor) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::UnsupportedVersion);
  }
  if (!valid_whole_game_plan_contract(tree, plan, accumulator.target_normalized_nashconv) ||
      accumulator.tree_fingerprint != tree.fingerprint ||
      accumulator.blueprint_fingerprint != plan.blueprint_fingerprint ||
      accumulator.plan_fingerprint != plan.fingerprint || accumulator.fingerprint.empty() ||
      accumulator.fingerprint !=
          fingerprint_hu_preflop_whole_game_coverage_accumulator(accumulator) ||
      accumulator.contribution_chain_fingerprint.empty() ||
      accumulator.task_side_coverage.size() != accumulator.task_joint_probabilities.size() ||
      accumulator.task_side_profile_utility_antes.size() !=
          accumulator.task_side_coverage.size() * 2U ||
      accumulator.task_local_continuation_identity_hashes.size() !=
          accumulator.task_side_coverage.size() ||
      accumulator.task_side_coverage.size() != plan.canonical_public_flop_roots ||
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
                 accumulator.continuation_checkpoint_fingerprint.empty())) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }

  std::uint64_t validated_boundaries = 0U;
  std::uint64_t fully_covered_tasks = 0U;
  double covered_probability = 0.0;
  for (std::size_t task = 0U; task < accumulator.task_side_coverage.size(); ++task) {
    const auto expected_task = canonical_flop_task_at(plan, task);
    if (!expected_task) {
      return Result<bool, HuPreflopError>::failure(expected_task.error());
    }
    const auto mask = accumulator.task_side_coverage[task];
    const auto probability = accumulator.task_joint_probabilities[task];
    const auto first_utility = accumulator.task_side_profile_utility_antes[task * 2U];
    const auto second_utility = accumulator.task_side_profile_utility_antes[task * 2U + 1U];
    const auto local_identity = accumulator.task_local_continuation_identity_hashes[task];
    if (mask > 0x3U || !std::isfinite(probability) || probability < 0.0 ||
        !std::isfinite(first_utility) || !std::isfinite(second_utility) ||
        ((mask == 0U) != (local_identity == 0U)) || ((mask & 0x1U) == 0U && first_utility != 0.0) ||
        ((mask & 0x2U) == 0U && second_utility != 0.0) ||
        std::abs(probability - expected_task.value().joint_probability) >
            probability_tolerance *
                std::max(1.0, std::abs(expected_task.value().joint_probability))) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    validated_boundaries += std::popcount(static_cast<unsigned>(mask));
    if (mask == 0x3U) {
      ++fully_covered_tasks;
      covered_probability += probability;
    }
  }
  if (!std::isfinite(covered_probability) ||
      accumulator.validated_boundary_count != validated_boundaries ||
      accumulator.fully_covered_task_count != fully_covered_tasks ||
      std::abs(accumulator.covered_postflop_probability - covered_probability) >
          probability_tolerance * std::max(1.0, std::abs(covered_probability))) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<bool, HuPreflopError> accumulate_hu_preflop_whole_game_boundary(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    HuPreflopWholeGameCoverageAccumulator &accumulator, const HuPreflopFlopBoundary &boundary) {
  if (accumulator.major != HuPreflopWholeGameCoverageAccumulator::format_major ||
      accumulator.minor != HuPreflopWholeGameCoverageAccumulator::format_minor) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::UnsupportedVersion);
  }
  if (tree.fingerprint != plan.tree_fingerprint ||
      accumulator.tree_fingerprint != tree.fingerprint ||
      accumulator.blueprint_fingerprint != plan.blueprint_fingerprint ||
      accumulator.plan_fingerprint != plan.fingerprint || accumulator.fingerprint.empty() ||
      !std::isfinite(accumulator.target_normalized_nashconv) ||
      accumulator.target_normalized_nashconv < 0.0 ||
      accumulator.fingerprint !=
          fingerprint_hu_preflop_whole_game_coverage_accumulator(accumulator) ||
      accumulator.task_side_coverage.size() != accumulator.task_joint_probabilities.size() ||
      accumulator.task_side_profile_utility_antes.size() !=
          accumulator.task_side_coverage.size() * 2U ||
      accumulator.task_local_continuation_identity_hashes.size() !=
          accumulator.task_side_coverage.size() ||
      accumulator.task_side_coverage.size() != plan.canonical_public_flop_roots ||
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
      accumulator.validated_boundary_count >=
          static_cast<std::uint64_t>(accumulator.task_side_coverage.size()) * 2U) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto valid_boundary = validate_hu_preflop_flop_boundary(tree, plan, boundary);
  if (!valid_boundary) {
    return Result<bool, HuPreflopError>::failure(valid_boundary.error());
  }
  if (accumulator.validated_boundary_count != 0U &&
      (accumulator.blueprint_iterations != boundary.blueprint_iterations ||
       accumulator.continuation_checkpoint_fingerprint !=
           boundary.continuation_checkpoint_fingerprint)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto task = whole_game_task_index(plan, boundary.entry_node, boundary.flop);
  if (task == std::numeric_limits<std::size_t>::max() ||
      task >= accumulator.task_side_coverage.size()) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto side = static_cast<std::uint8_t>(1U << boundary.resolving_player);
  const auto previous_mask = accumulator.task_side_coverage[task];
  const auto local_identity_hash = fnv1a(boundary.continuation_fingerprint);
  const auto previous_local_identity_hash =
      accumulator.task_local_continuation_identity_hashes[task];
  if (local_identity_hash == 0U || previous_mask > 0x3U || (previous_mask & side) != 0U ||
      (previous_mask == 0U ? previous_local_identity_hash != 0U
                           : previous_local_identity_hash != local_identity_hash)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto new_mask = static_cast<std::uint8_t>(previous_mask | side);
  const auto completes_task = new_mask == 0x3U;
  const auto covered_probability =
      accumulator.covered_postflop_probability +
      (completes_task ? accumulator.task_joint_probabilities[task] : 0.0);
  if (!std::isfinite(covered_probability)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
  }

  const auto *entry = find_entry(plan, boundary.entry_node);
  const auto flop_index = plan.canonical_flops == 0U
                              ? std::numeric_limits<std::size_t>::max()
                              : task % static_cast<std::size_t>(plan.canonical_flops);
  if (entry == nullptr || flop_index >= plan.canonical_flop_catalog.size() ||
      plan.canonical_flop_catalog[flop_index].cards != boundary.flop) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  HuPreflopCompensatedCounterfactualValue profile_contribution;
  for (const auto &value : boundary.values) {
    const auto own_reach = entry->own_sequence_reach[boundary.opponent][value.opponent_combo];
    const auto utility =
        static_cast<double>(plan.canonical_flop_catalog[flop_index].physical_outcome_count) *
        own_reach * value.counterfactual_reach * value.blueprint_counterfactual_value_antes;
    if (!std::isfinite(own_reach) || own_reach < 0.0 || !std::isfinite(utility)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
    compensated_add(utility, profile_contribution.weighted_utility_sum_antes,
                    profile_contribution.weighted_utility_compensation_antes);
  }
  const auto profile_utility = profile_contribution.weighted_utility_sum_antes +
                               profile_contribution.weighted_utility_compensation_antes;
  const auto profile_slot = task * 2U + boundary.resolving_player;
  if (!std::isfinite(profile_utility) ||
      accumulator.task_side_profile_utility_antes[profile_slot] != 0.0) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
  }

  accumulator.task_side_coverage[task] = new_mask;
  accumulator.coverage_state_hash ^=
      whole_game_coverage_slot_hash(static_cast<std::uint64_t>(task), previous_mask);
  accumulator.coverage_state_hash ^=
      whole_game_coverage_slot_hash(static_cast<std::uint64_t>(task), new_mask);
  accumulator.continuation_profile_hash ^=
      whole_game_boundary_profile_slot_hash(task, boundary.resolving_player, boundary.fingerprint);
  if (previous_mask == 0U) {
    accumulator.local_continuation_identity_state_hash ^=
        whole_game_local_continuation_slot_hash(static_cast<std::uint64_t>(task), 0U);
    accumulator.task_local_continuation_identity_hashes[task] = local_identity_hash;
    accumulator.local_continuation_identity_state_hash ^= whole_game_local_continuation_slot_hash(
        static_cast<std::uint64_t>(task), local_identity_hash);
  }
  accumulator.profile_utility_state_hash ^=
      whole_game_profile_utility_slot_hash(static_cast<std::uint64_t>(profile_slot), 0.0);
  accumulator.task_side_profile_utility_antes[profile_slot] = profile_utility;
  accumulator.profile_utility_state_hash ^= whole_game_profile_utility_slot_hash(
      static_cast<std::uint64_t>(profile_slot), profile_utility);
  if (accumulator.validated_boundary_count == 0U) {
    accumulator.blueprint_iterations = boundary.blueprint_iterations;
    accumulator.continuation_checkpoint_fingerprint = boundary.continuation_checkpoint_fingerprint;
  }
  ++accumulator.validated_boundary_count;
  if (completes_task) {
    ++accumulator.fully_covered_task_count;
  }
  accumulator.covered_postflop_probability = covered_probability;
  accumulator.contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a(accumulator.contribution_chain_fingerprint + "|" + boundary.fingerprint));
  accumulator.fingerprint = fingerprint_hu_preflop_whole_game_coverage_accumulator(accumulator);
  return Result<bool, HuPreflopError>::success(true);
}

Result<std::uint64_t, HuPreflopError>
stream_hu_preflop_whole_game_boundaries(const HuPreflopTree &tree,
                                        const HuPreflopDecompositionPlan &plan,
                                        HuPreflopWholeGameCoverageAccumulator &accumulator,
                                        const HuPreflopWholeGameBoundaryProvider &provider,
                                        const HuPreflopWholeGameCheckpointSink &checkpoint_sink) {
  if (!provider) {
    return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto valid_accumulator =
      validate_hu_preflop_whole_game_coverage_accumulator(tree, plan, accumulator);
  if (!valid_accumulator) {
    return Result<std::uint64_t, HuPreflopError>::failure(valid_accumulator.error());
  }
  std::uint64_t committed_tasks = 0U;
  for (std::size_t task_index = 0U; task_index < accumulator.task_side_coverage.size();
       ++task_index) {
    const auto current_mask = accumulator.task_side_coverage[task_index];
    if (current_mask > 0x3U) {
      return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto missing_mask = static_cast<std::uint8_t>((~current_mask) & 0x3U);
    if (missing_mask == 0U) {
      continue;
    }
    auto task = canonical_flop_task_at(plan, task_index);
    if (!task) {
      return Result<std::uint64_t, HuPreflopError>::failure(task.error());
    }

    HuPreflopWholeGameBoundaryRequest request;
    request.task_index = static_cast<std::uint64_t>(task_index);
    request.task = std::move(task.value());
    request.missing_resolver_mask = missing_mask;
    auto provided = provider(request);
    if (!provided) {
      return Result<std::uint64_t, HuPreflopError>::failure(provided.error());
    }
    auto boundaries = std::move(provided.value());
    if (boundaries.size() !=
        static_cast<std::size_t>(std::popcount(static_cast<unsigned>(missing_mask)))) {
      return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }

    std::array<bool, 2U> seen_resolver{};
    for (const auto &boundary : boundaries) {
      if (boundary.entry_node != request.task.entry_node || boundary.flop != request.task.flop ||
          boundary.resolving_player >= seen_resolver.size()) {
        return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      const auto side = static_cast<std::uint8_t>(1U << boundary.resolving_player);
      if ((missing_mask & side) == 0U || seen_resolver[boundary.resolving_player]) {
        return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      const auto valid_boundary = validate_hu_preflop_flop_boundary(tree, plan, boundary);
      if (!valid_boundary) {
        return Result<std::uint64_t, HuPreflopError>::failure(valid_boundary.error());
      }
      seen_resolver[boundary.resolving_player] = true;
    }
    std::ranges::sort(boundaries, {}, &HuPreflopFlopBoundary::resolving_player);

    auto candidate = accumulator;
    for (const auto &boundary : boundaries) {
      const auto accumulated =
          accumulate_hu_preflop_whole_game_boundary(tree, plan, candidate, boundary);
      if (!accumulated) {
        return Result<std::uint64_t, HuPreflopError>::failure(accumulated.error());
      }
    }
    if (candidate.task_side_coverage[task_index] != 0x3U) {
      return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    if (checkpoint_sink) {
      const auto checkpointed = checkpoint_sink(candidate);
      if (!checkpointed) {
        return Result<std::uint64_t, HuPreflopError>::failure(checkpointed.error());
      }
      if (!checkpointed.value()) {
        return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::IoFailure);
      }
    }
    accumulator = std::move(candidate);
    ++committed_tasks;
  }

  const auto valid_completed_accumulator =
      validate_hu_preflop_whole_game_coverage_accumulator(tree, plan, accumulator);
  if (!valid_completed_accumulator) {
    return Result<std::uint64_t, HuPreflopError>::failure(valid_completed_accumulator.error());
  }
  return Result<std::uint64_t, HuPreflopError>::success(committed_tasks);
}

Result<HuPreflopWholeGameCertification, HuPreflopError>
certify_hu_preflop_whole_game(const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
                              const std::vector<HuPreflopFlopBoundary> &boundaries,
                              const HuPreflopGlobalBestResponseEvidence *global_best_response,
                              const double target_normalized_nashconv) {
  auto accumulator =
      make_hu_preflop_whole_game_coverage_accumulator(tree, plan, target_normalized_nashconv);
  if (!accumulator) {
    return Result<HuPreflopWholeGameCertification, HuPreflopError>::failure(accumulator.error());
  }
  for (const auto &boundary : boundaries) {
    const auto accumulated =
        accumulate_hu_preflop_whole_game_boundary(tree, plan, accumulator.value(), boundary);
    if (!accumulated) {
      return Result<HuPreflopWholeGameCertification, HuPreflopError>::failure(accumulated.error());
    }
  }
  return finalize_hu_preflop_whole_game_certification(tree, plan, accumulator.value(),
                                                      global_best_response);
}

Result<PostflopTreeConfig, HuPreflopError>
make_hu_preflop_postflop_config(const HuPreflopTree &tree, const std::uint32_t entry_node,
                                const std::array<CardId, 3> &requested_flop) {
  const auto flop = canonical_flop(requested_flop);
  if (entry_node >= tree.nodes.size() || !valid_flop(flop)) {
    return Result<PostflopTreeConfig, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &entry = tree.nodes[entry_node];
  if (entry.kind != HuPreflopNodeKind::PostflopEntry ||
      entry.state.remaining_stacks[0] != entry.state.remaining_stacks[1] ||
      entry.state.remaining_stacks[0].units() <= 0) {
    return Result<PostflopTreeConfig, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  PostflopTreeConfig config;
  config.flop = flop;
  config.initial_pot = entry.state.pot;
  config.effective_stack = entry.state.remaining_stacks[0];
  config.rake = tree.config.rake;
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                         tree.config.postflop_sizes.end());
        // The CO40 public skeleton proves that stack exhaustion occurs at four
        // raises per street; four is also the serialized postflop tree limit.
        scenario.raise_depth = 4U;
        scenario.all_in_mode = AllInMode::Add;
        scenario.all_in_threshold = PotPercentage::from_basis_points(100'000).value();
        scenario.minimum_bet = tree.config.postflop_minimum_bet;
      }
    }
  }
  const auto valid = validate_tree_config(config);
  return valid ? Result<PostflopTreeConfig, HuPreflopError>::success(std::move(config))
               : Result<PostflopTreeConfig, HuPreflopError>::failure(
                     HuPreflopError::InvalidConfiguration);
}

Result<std::vector<HuPostflopBettingRootShape>, HuPreflopError>
enumerate_hu_postflop_betting_root_shapes(const HuPreflopTree &tree, const Street target_street) {
  if (!validate_hu_preflop_config(tree.config) ||
      (target_street != Street::Turn && target_street != Street::River)) {
    return Result<std::vector<HuPostflopBettingRootShape>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  ActionConfig action_config;
  action_config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                        tree.config.postflop_sizes.end());
  action_config.raise_depth = maximum_core_raise_depth;
  action_config.minimum_bet = tree.config.postflop_minimum_bet;
  action_config.all_in_mode = AllInMode::Add;
  action_config.all_in_threshold = PotPercentage::from_basis_points(100'000).value();

  std::vector<HuPostflopBettingRootShape> result;
  std::vector<Action> action_history;
  std::function<bool(const PublicState &, std::uint32_t, std::uint64_t)> visit;
  visit = [&](const PublicState &state, const std::uint32_t entry_node,
              const std::uint64_t history) {
    if (state.status == HandStatus::Folded || state.status == HandStatus::Showdown ||
        state.status == HandStatus::AllInRunout) {
      return true;
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_street(state);
      if (!advanced) {
        return false;
      }
      const auto next_history = fnv1a(
          "street:" + std::to_string(static_cast<unsigned>(advanced.value().street)), history);
      if (advanced.value().street == target_street) {
        result.push_back({entry_node, target_street, advanced.value(), action_history,
                          "fnv1a64:" + hex64(next_history)});
        return true;
      }
      return visit(advanced.value(), entry_node, next_history);
    }
    if (state.status != HandStatus::InProgress) {
      return false;
    }
    const auto actions = legal_actions(state, action_config);
    if (!actions || actions.value().empty()) {
      return false;
    }
    for (const auto &action : actions.value()) {
      const auto next = apply_action(state, action, action_config);
      if (!next) {
        return false;
      }
      const auto token = std::to_string(static_cast<unsigned>(action.type)) + ":" +
                         std::to_string(action.amount.units()) + ":" +
                         std::to_string(action.requested_basis_points) + ":" +
                         std::to_string(static_cast<unsigned>(action.all_in_kind));
      action_history.push_back(action);
      if (!visit(next.value(), entry_node, fnv1a(token, history))) {
        return false;
      }
      action_history.pop_back();
    }
    return true;
  };

  for (const auto &entry : tree.nodes) {
    if (entry.kind != HuPreflopNodeKind::PostflopEntry ||
        !visit(entry.state, entry.id, fnv1a("entry:" + std::to_string(entry.id)))) {
      if (entry.kind == HuPreflopNodeKind::PostflopEntry) {
        return Result<std::vector<HuPostflopBettingRootShape>, HuPreflopError>::failure(
            HuPreflopError::GameFailure);
      }
    }
  }
  if (result.empty()) {
    return Result<std::vector<HuPostflopBettingRootShape>, HuPreflopError>::failure(
        HuPreflopError::GameFailure);
  }
  return Result<std::vector<HuPostflopBettingRootShape>, HuPreflopError>::success(
      std::move(result));
}

Result<PublicState, HuPreflopError>
replay_hu_postflop_betting_root_shape(const HuPreflopTree &tree,
                                      const HuPostflopBettingRootShape &shape) {
  if (!validate_hu_preflop_config(tree.config) || shape.entry_node >= tree.nodes.size() ||
      tree.nodes[shape.entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      (shape.street != Street::Turn && shape.street != Street::River) ||
      shape.state.street != shape.street || shape.state.status != HandStatus::InProgress ||
      shape.state.board_mask != 0U || shape.history_fingerprint.empty()) {
    return Result<PublicState, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  ActionConfig action_config;
  action_config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                        tree.config.postflop_sizes.end());
  action_config.raise_depth = maximum_core_raise_depth;
  action_config.minimum_bet = tree.config.postflop_minimum_bet;
  action_config.all_in_mode = AllInMode::Add;
  action_config.all_in_threshold = PotPercentage::from_basis_points(100'000).value();

  auto state = tree.nodes[shape.entry_node].state;
  auto history = fnv1a("entry:" + std::to_string(shape.entry_node));
  for (const auto &action : shape.action_history) {
    while (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_street(state);
      if (!advanced || advanced.value().street >= shape.street) {
        return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      state = advanced.value();
      history = fnv1a("street:" + std::to_string(static_cast<unsigned>(state.street)), history);
    }
    if (state.status != HandStatus::InProgress) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto legal = legal_actions(state, action_config);
    if (!legal || std::ranges::find(legal.value(), action) == legal.value().end()) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto next = apply_action(state, action, action_config);
    if (!next) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    state = next.value();
    const auto token = std::to_string(static_cast<unsigned>(action.type)) + ":" +
                       std::to_string(action.amount.units()) + ":" +
                       std::to_string(action.requested_basis_points) + ":" +
                       std::to_string(static_cast<unsigned>(action.all_in_kind));
    history = fnv1a(token, history);
  }
  while (state.status == HandStatus::StreetComplete && state.street < shape.street) {
    const auto advanced = advance_street(state);
    if (!advanced) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    state = advanced.value();
    history = fnv1a("street:" + std::to_string(static_cast<unsigned>(state.street)), history);
  }
  if (state != shape.state || "fnv1a64:" + hex64(history) != shape.history_fingerprint) {
    return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<PublicState, HuPreflopError>::success(std::move(state));
}

Result<std::vector<HuPostflopUpperStreetTerminalShape>, HuPreflopError>
enumerate_hu_postflop_upper_street_terminal_shapes(const HuPreflopTree &tree) {
  if (!validate_hu_preflop_config(tree.config)) {
    return Result<std::vector<HuPostflopUpperStreetTerminalShape>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  ActionConfig action_config;
  action_config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                        tree.config.postflop_sizes.end());
  action_config.raise_depth = maximum_core_raise_depth;
  action_config.minimum_bet = tree.config.postflop_minimum_bet;
  action_config.all_in_mode = AllInMode::Add;
  action_config.all_in_threshold = PotPercentage::from_basis_points(100'000).value();

  std::vector<HuPostflopUpperStreetTerminalShape> result;
  std::map<std::uint32_t, std::uint64_t> next_terminal_ordinal;
  std::vector<Action> action_history;
  std::function<bool(const PublicState &, std::uint32_t, std::uint64_t)> visit;
  visit = [&](const PublicState &state, const std::uint32_t entry_node,
              const std::uint64_t history) {
    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout) {
      if (state.street != Street::Flop && state.street != Street::Turn) {
        return false;
      }
      result.push_back({entry_node, next_terminal_ordinal[entry_node]++, state.street, state,
                        action_history, "fnv1a64:" + hex64(history)});
      return true;
    }
    if (state.status == HandStatus::Showdown) {
      return state.street == Street::River;
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_street(state);
      if (!advanced) {
        return false;
      }
      if (advanced.value().street == Street::River) {
        return true;
      }
      const auto next_history = fnv1a(
          "street:" + std::to_string(static_cast<unsigned>(advanced.value().street)), history);
      return visit(advanced.value(), entry_node, next_history);
    }
    if (state.status != HandStatus::InProgress) {
      return false;
    }
    const auto actions = legal_actions(state, action_config);
    if (!actions || actions.value().empty()) {
      return false;
    }
    for (const auto &action : actions.value()) {
      const auto next = apply_action(state, action, action_config);
      if (!next) {
        return false;
      }
      const auto token = std::to_string(static_cast<unsigned>(action.type)) + ":" +
                         std::to_string(action.amount.units()) + ":" +
                         std::to_string(action.requested_basis_points) + ":" +
                         std::to_string(static_cast<unsigned>(action.all_in_kind));
      action_history.push_back(action);
      const auto accepted = visit(next.value(), entry_node, fnv1a(token, history));
      action_history.pop_back();
      if (!accepted) {
        return false;
      }
    }
    return true;
  };

  for (const auto &entry : tree.nodes) {
    if (entry.kind == HuPreflopNodeKind::PostflopEntry &&
        !visit(entry.state, entry.id, fnv1a("entry:" + std::to_string(entry.id)))) {
      return Result<std::vector<HuPostflopUpperStreetTerminalShape>, HuPreflopError>::failure(
          HuPreflopError::GameFailure);
    }
  }
  return result.empty()
             ? Result<std::vector<HuPostflopUpperStreetTerminalShape>, HuPreflopError>::failure(
                   HuPreflopError::GameFailure)
             : Result<std::vector<HuPostflopUpperStreetTerminalShape>, HuPreflopError>::success(
                   std::move(result));
}

Result<PublicState, HuPreflopError>
replay_hu_postflop_upper_street_terminal_shape(const HuPreflopTree &tree,
                                               const HuPostflopUpperStreetTerminalShape &shape) {
  if (!validate_hu_preflop_config(tree.config) || shape.entry_node >= tree.nodes.size() ||
      tree.nodes[shape.entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      (shape.street != Street::Flop && shape.street != Street::Turn) ||
      shape.state.street != shape.street ||
      (shape.state.status != HandStatus::Folded && shape.state.status != HandStatus::AllInRunout) ||
      shape.state.board_mask != 0U || shape.action_history.empty() ||
      shape.history_fingerprint.empty()) {
    return Result<PublicState, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  ActionConfig action_config;
  action_config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                        tree.config.postflop_sizes.end());
  action_config.raise_depth = maximum_core_raise_depth;
  action_config.minimum_bet = tree.config.postflop_minimum_bet;
  action_config.all_in_mode = AllInMode::Add;
  action_config.all_in_threshold = PotPercentage::from_basis_points(100'000).value();

  auto state = tree.nodes[shape.entry_node].state;
  auto history = fnv1a("entry:" + std::to_string(shape.entry_node));
  for (const auto &action : shape.action_history) {
    while (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_street(state);
      if (!advanced || advanced.value().street > shape.street) {
        return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      state = advanced.value();
      history = fnv1a("street:" + std::to_string(static_cast<unsigned>(state.street)), history);
    }
    if (state.status != HandStatus::InProgress) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto legal = legal_actions(state, action_config);
    if (!legal || std::ranges::find(legal.value(), action) == legal.value().end()) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto next = apply_action(state, action, action_config);
    if (!next) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    state = next.value();
    const auto token = std::to_string(static_cast<unsigned>(action.type)) + ":" +
                       std::to_string(action.amount.units()) + ":" +
                       std::to_string(action.requested_basis_points) + ":" +
                       std::to_string(static_cast<unsigned>(action.all_in_kind));
    history = fnv1a(token, history);
  }
  if (state != shape.state || "fnv1a64:" + hex64(history) != shape.history_fingerprint) {
    return Result<PublicState, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<PublicState, HuPreflopError>::success(std::move(state));
}

namespace {

struct ReplayedTerminalPath {
  PublicState state{};
  std::array<HuPreflopComboReach, 2> postflop_action_reach{};
};

ActionConfig hu_preflop_postflop_action_config(const HuPreflopTree &tree) {
  ActionConfig result;
  result.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                 tree.config.postflop_sizes.end());
  result.raise_depth = maximum_core_raise_depth;
  result.minimum_bet = tree.config.postflop_minimum_bet;
  result.all_in_mode = AllInMode::Add;
  result.all_in_threshold = PotPercentage::from_basis_points(100'000).value();
  return result;
}

Result<ReplayedTerminalPath, HuPreflopError>
replay_terminal_path_with_cards(const HuPreflopTree &tree,
                                const HuPostflopUpperStreetTerminalShape &shape,
                                const std::array<CardId, 3> &flop, const std::optional<CardId> turn,
                                const HuPostflopActionProbabilityProvider &probability_provider) {
  if (!probability_provider || !valid_flop(flop) || shape.entry_node >= tree.nodes.size() ||
      tree.nodes[shape.entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      (shape.street == Street::Flop && turn.has_value()) ||
      (shape.street == Street::Turn &&
       (!turn.has_value() || (turn->mask() & flop_mask(flop)) != 0U))) {
    return Result<ReplayedTerminalPath, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto structural = replay_hu_postflop_upper_street_terminal_shape(tree, shape);
  if (!structural) {
    return Result<ReplayedTerminalPath, HuPreflopError>::failure(structural.error());
  }

  ReplayedTerminalPath result;
  result.postflop_action_reach[0].fill(1.0);
  result.postflop_action_reach[1].fill(1.0);
  auto state = tree.nodes[shape.entry_node].state;
  state.board_mask = flop_mask(flop);
  const auto action_config = hu_preflop_postflop_action_config(tree);
  const auto masks = combo_masks();
  std::vector<Action> action_prefix;
  action_prefix.reserve(shape.action_history.size());
  bool zero_joint_action_reach = false;

  const auto advance_with_cards = [&]() -> bool {
    const auto advanced = advance_street(state);
    if (!advanced) {
      return false;
    }
    state = advanced.value();
    if (state.street == Street::Turn) {
      if (!turn.has_value()) {
        return false;
      }
      state.board_mask |= turn->mask();
    }
    return state.street != Street::River;
  };

  for (const auto &action : shape.action_history) {
    while (state.status == HandStatus::StreetComplete) {
      if (!advance_with_cards()) {
        return Result<ReplayedTerminalPath, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
    }
    if (state.status != HandStatus::InProgress || state.player_to_act > 1U) {
      return Result<ReplayedTerminalPath, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto legal = legal_actions(state, action_config);
    if (!legal || std::ranges::find(legal.value(), action) == legal.value().end()) {
      return Result<ReplayedTerminalPath, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto actor = state.player_to_act;
    if (!zero_joint_action_reach) {
      bool actor_has_positive_reach = false;
      for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
        if ((masks[combo] & state.board_mask) != 0U) {
          continue;
        }
        const auto probability = probability_provider(state, std::span<const Action>{action_prefix},
                                                      action, static_cast<ComboId>(combo));
        if (!probability || !std::isfinite(probability.value()) || probability.value() < 0.0 ||
            probability.value() > 1.0) {
          return Result<ReplayedTerminalPath, HuPreflopError>::failure(
              probability ? HuPreflopError::NumericalFailure : probability.error());
        }
        result.postflop_action_reach[actor][combo] *= probability.value();
        if (!std::isfinite(result.postflop_action_reach[actor][combo])) {
          return Result<ReplayedTerminalPath, HuPreflopError>::failure(
              HuPreflopError::NumericalFailure);
        }
        actor_has_positive_reach =
            actor_has_positive_reach || result.postflop_action_reach[actor][combo] > 0.0;
      }
      zero_joint_action_reach = !actor_has_positive_reach;
    }
    const auto next = apply_action(state, action, action_config);
    if (!next) {
      return Result<ReplayedTerminalPath, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    state = next.value();
    action_prefix.push_back(action);
  }

  auto expected = shape.state;
  expected.board_mask = flop_mask(flop) | (turn.has_value() ? turn->mask() : 0U);
  if (state != expected || action_prefix != shape.action_history) {
    return Result<ReplayedTerminalPath, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  result.state = std::move(state);
  return Result<ReplayedTerminalPath, HuPreflopError>::success(std::move(result));
}

bool valid_task_value_rows(const std::array<std::vector<HuPreflopRiverTaskAggregateValue>, 2> &rows,
                           const std::array<CardId, 3> &flop) {
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(flop);
  for (const auto &resolver : rows) {
    if (resolver.size() != live_combos_per_flop) {
      return false;
    }
    std::array<bool, 630U> observed{};
    for (const auto &value : resolver) {
      if (value.opponent_combo >= masks.size() || observed[value.opponent_combo] ||
          (masks[value.opponent_combo] & board_mask) != 0U ||
          !std::isfinite(value.weighted_counterfactual_reach) ||
          value.weighted_counterfactual_reach < 0.0 ||
          !std::isfinite(value.weighted_counterfactual_utility_antes) ||
          !std::isfinite(value.conditional_value_antes) ||
          value.positive_reach != (value.weighted_counterfactual_reach > 0.0)) {
        return false;
      }
      if (value.weighted_counterfactual_reach == 0.0) {
        if (value.weighted_counterfactual_utility_antes != 0.0 ||
            value.conditional_value_antes != 0.0) {
          return false;
        }
      } else {
        const auto expected =
            value.weighted_counterfactual_utility_antes / value.weighted_counterfactual_reach;
        const auto tolerance =
            1.0e-12 * std::max({1.0, std::abs(expected), std::abs(value.conditional_value_antes)});
        if (std::abs(expected - value.conditional_value_antes) > tolerance) {
          return false;
        }
      }
      observed[value.opponent_combo] = true;
    }
  }
  return true;
}

bool valid_best_response_combo_value_rows(const std::vector<HuPreflopBestResponseComboValue> &rows,
                                          const std::array<CardId, 3> &flop) {
  if (rows.size() != live_combos_per_flop) {
    return false;
  }
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(flop);
  std::array<bool, 630U> observed{};
  for (const auto &value : rows) {
    if (value.responding_combo >= masks.size() || observed[value.responding_combo] ||
        (masks[value.responding_combo] & board_mask) != 0U ||
        !std::isfinite(value.weighted_counterfactual_reach) ||
        value.weighted_counterfactual_reach < 0.0 ||
        !std::isfinite(value.weighted_counterfactual_utility_antes) ||
        !std::isfinite(value.conditional_value_antes) ||
        value.positive_reach != (value.weighted_counterfactual_reach > 0.0)) {
      return false;
    }
    if (value.positive_reach) {
      const auto expected =
          value.weighted_counterfactual_utility_antes / value.weighted_counterfactual_reach;
      const auto tolerance =
          1.0e-12 * std::max({1.0, std::abs(expected), std::abs(value.conditional_value_antes)});
      if (std::abs(expected - value.conditional_value_antes) > tolerance) {
        return false;
      }
    } else if (value.weighted_counterfactual_utility_antes != 0.0 ||
               value.conditional_value_antes != 0.0) {
      return false;
    }
    observed[value.responding_combo] = true;
  }
  return std::count(observed.begin(), observed.end(), true) == live_combos_per_flop;
}

bool valid_best_response_leaf_values(const HuPreflopBestResponseLeafQuery &query,
                                     const std::vector<HuPreflopBestResponseComboValue> &rows) {
  if (query.responding_player > 1U || query.history_fingerprint.empty() ||
      !valid_best_response_combo_value_rows(rows, query.flop)) {
    return false;
  }
  const auto expected_board = flop_mask(query.flop) | (query.has_turn ? query.turn.mask() : 0U);
  if (query.state.board_mask != expected_board ||
      (query.has_turn && (query.turn.mask() & flop_mask(query.flop)) != 0U)) {
    return false;
  }
  if (query.kind == HuPreflopBestResponseLeafKind::UpperStreetTerminal) {
    if ((query.state.street != Street::Flop && query.state.street != Street::Turn) ||
        (query.state.status != HandStatus::Folded &&
         query.state.status != HandStatus::AllInRunout) ||
        query.has_turn != (query.state.street == Street::Turn)) {
      return false;
    }
  } else if (query.kind == HuPreflopBestResponseLeafKind::RiverContinuation) {
    if (!query.has_turn || query.state.street != Street::River ||
        query.state.status != HandStatus::InProgress) {
      return false;
    }
  } else {
    return false;
  }

  if (query.has_turn) {
    const auto masks = combo_masks();
    for (const auto &value : rows) {
      if ((masks[value.responding_combo] & query.turn.mask()) != 0U &&
          (value.weighted_counterfactual_reach != 0.0 ||
           value.weighted_counterfactual_utility_antes != 0.0 ||
           value.conditional_value_antes != 0.0 || value.positive_reach)) {
        return false;
      }
    }
  }
  return true;
}

bool valid_best_response_hand_class_rows(
    const std::vector<HuPreflopBestResponseHandClassValue> &rows) {
  if (rows.size() != hu_preflop_hand_class_count) {
    return false;
  }
  std::array<bool, hu_preflop_hand_class_count> observed{};
  for (const auto &value : rows) {
    if (value.responding_class >= observed.size() || observed[value.responding_class]) {
      return false;
    }
    if (!std::isfinite(value.weighted_counterfactual_reach) ||
        value.weighted_counterfactual_reach < 0.0 ||
        !std::isfinite(value.weighted_counterfactual_utility_antes) ||
        !std::isfinite(value.conditional_value_antes) ||
        value.positive_reach != (value.weighted_counterfactual_reach > 0.0)) {
      return false;
    }
    if (value.weighted_counterfactual_reach == 0.0) {
      if (value.weighted_counterfactual_utility_antes != 0.0 ||
          value.conditional_value_antes != 0.0) {
        return false;
      }
    } else {
      const auto expected =
          value.weighted_counterfactual_utility_antes / value.weighted_counterfactual_reach;
      const auto tolerance =
          1.0e-12 * std::max({1.0, std::abs(expected), std::abs(value.conditional_value_antes)});
      if (std::abs(expected - value.conditional_value_antes) > tolerance) {
        return false;
      }
    }
    observed[value.responding_class] = true;
  }
  return true;
}

Result<std::vector<HuPreflopBestResponseHandClassValue>, HuPreflopError>
reduce_best_response_hand_class_action_values(
    const std::span<const std::vector<HuPreflopBestResponseHandClassValue>> action_children,
    const bool responding_player_acts) {
  using ClassValue = HuPreflopBestResponseHandClassValue;
  if (action_children.empty()) {
    return Result<std::vector<ClassValue>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  std::vector<std::array<const ClassValue *, hu_preflop_hand_class_count>> indexed_children;
  indexed_children.reserve(action_children.size());
  for (const auto &child : action_children) {
    if (!valid_best_response_hand_class_rows(child)) {
      return Result<std::vector<ClassValue>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    std::array<const ClassValue *, hu_preflop_hand_class_count> indexed{};
    for (const auto &value : child) {
      indexed[value.responding_class] = &value;
    }
    indexed_children.push_back(indexed);
  }

  std::vector<ClassValue> result;
  result.reserve(hu_preflop_hand_class_count);
  for (std::size_t class_id = 0U; class_id < hu_preflop_hand_class_count; ++class_id) {
    if (responding_player_acts) {
      const auto *selected = indexed_children.front()[class_id];
      if (selected == nullptr) {
        return Result<std::vector<ClassValue>, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      for (std::size_t child = 1U; child < indexed_children.size(); ++child) {
        const auto *candidate = indexed_children[child][class_id];
        if (candidate == nullptr) {
          return Result<std::vector<ClassValue>, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        const auto reach_tolerance =
            1.0e-12 * std::max({1.0, std::abs(selected->weighted_counterfactual_reach),
                                std::abs(candidate->weighted_counterfactual_reach)});
        if (std::abs(candidate->weighted_counterfactual_reach -
                     selected->weighted_counterfactual_reach) > reach_tolerance) {
          return Result<std::vector<ClassValue>, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        if (candidate->weighted_counterfactual_utility_antes >
            selected->weighted_counterfactual_utility_antes) {
          selected = candidate;
        }
      }
      result.push_back(*selected);
      continue;
    }

    HuPreflopCompensatedCounterfactualValue accumulated;
    for (const auto &child : indexed_children) {
      const auto *value = child[class_id];
      if (value == nullptr) {
        return Result<std::vector<ClassValue>, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      compensated_add(value->weighted_counterfactual_reach, accumulated.weighted_reach_sum,
                      accumulated.weighted_reach_compensation);
      compensated_add(value->weighted_counterfactual_utility_antes,
                      accumulated.weighted_utility_sum_antes,
                      accumulated.weighted_utility_compensation_antes);
    }
    const auto reach = accumulated.weighted_reach_sum + accumulated.weighted_reach_compensation;
    const auto utility =
        accumulated.weighted_utility_sum_antes + accumulated.weighted_utility_compensation_antes;
    if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
        (reach == 0.0 && utility != 0.0)) {
      return Result<std::vector<ClassValue>, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    result.push_back({static_cast<HandClassId>(class_id), reach, utility,
                      reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
  }
  return valid_best_response_hand_class_rows(result)
             ? Result<std::vector<ClassValue>, HuPreflopError>::success(std::move(result))
             : Result<std::vector<ClassValue>, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<std::vector<std::uint64_t>, HuPreflopError>
expected_river_best_response_leaf_roots(const HuPreflopRiverRootCatalog &catalog,
                                        const HuPreflopRiverBatchPlan &batch_plan,
                                        const HuPreflopBestResponseLeafQuery &query) {
  if (!valid_river_batch_plan_for_catalog(batch_plan, catalog) ||
      query.kind != HuPreflopBestResponseLeafKind::RiverContinuation || !query.has_turn ||
      query.responding_player > 1U || query.task_span_index >= catalog.task_spans.size() ||
      query.state.street != Street::River || query.state.status != HandStatus::InProgress ||
      query.history_fingerprint.empty()) {
    return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &task = catalog.task_spans[query.task_span_index];
  if (task.first_board_index >= catalog.canonical_boards.size() ||
      query.entry_node != task.entry_node ||
      query.flop != catalog.canonical_boards[task.first_board_index].flop ||
      (query.turn.mask() & flop_mask(query.flop)) != 0U) {
    return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto shape_begin =
      catalog.river_shapes.begin() + static_cast<std::ptrdiff_t>(task.first_shape_index);
  const auto shape_end = shape_begin + static_cast<std::ptrdiff_t>(task.shape_count);
  const auto shape = std::ranges::find_if(
      std::ranges::subrange(shape_begin, shape_end), [&](const auto &candidate) {
        return candidate.entry_node == query.entry_node &&
               candidate.history_fingerprint == query.history_fingerprint;
      });
  if (shape == shape_end || shape->action_history != query.action_history) {
    return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  auto expected_state = shape->state;
  expected_state.board_mask = flop_mask(query.flop) | query.turn.mask();
  if (query.state != expected_state) {
    return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  const auto local_shape = static_cast<std::uint64_t>(std::distance(shape_begin, shape));
  std::uint64_t roots_per_shape = 0U;
  std::uint64_t shape_root_offset = 0U;
  if (!checked_multiply(task.board_count, 2U, roots_per_shape) ||
      !checked_multiply(local_shape, roots_per_shape, shape_root_offset)) {
    return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  std::vector<std::uint64_t> expected;
  expected.reserve(32U);
  std::array<std::uint8_t, 36U> physical_river_coverage{};
  for (std::uint64_t board_offset = 0U; board_offset < task.board_count; ++board_offset) {
    const auto &board = catalog.canonical_boards[task.first_board_index + board_offset];
    const auto lifts = ordered_runout_orbit_lifts(board.flop, board.turn, board.river);
    if (!lifts) {
      return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(lifts.error());
    }
    bool represents_turn = false;
    for (const auto &permutation : lifts.value()) {
      const auto physical_turn = static_cast<std::uint8_t>((board.turn.value() / 4U) * 4U +
                                                           permutation[board.turn.value() % 4U]);
      if (physical_turn != query.turn.value()) {
        continue;
      }
      represents_turn = true;
      const auto physical_river = static_cast<std::uint8_t>((board.river.value() / 4U) * 4U +
                                                            permutation[board.river.value() % 4U]);
      if (physical_river >= physical_river_coverage.size() ||
          physical_river_coverage[physical_river] == std::numeric_limits<std::uint8_t>::max()) {
        return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      ++physical_river_coverage[physical_river];
    }
    if (represents_turn) {
      const auto resolving_player = static_cast<std::uint64_t>(1U - query.responding_player);
      if (task.first_resolver_root >
              std::numeric_limits<std::uint64_t>::max() - shape_root_offset ||
          task.first_resolver_root + shape_root_offset >
              std::numeric_limits<std::uint64_t>::max() - board_offset * 2U ||
          task.first_resolver_root + shape_root_offset + board_offset * 2U >
              std::numeric_limits<std::uint64_t>::max() - resolving_player) {
        return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
            HuPreflopError::CountOverflow);
      }
      expected.push_back(task.first_resolver_root + shape_root_offset + board_offset * 2U +
                         resolving_player);
    }
  }
  const auto board_mask = flop_mask(query.flop) | query.turn.mask();
  for (std::size_t card = 0U; card < physical_river_coverage.size(); ++card) {
    const auto physical = CardId::from_index(static_cast<std::uint8_t>(card));
    const auto expected_count = physical && (physical.value().mask() & board_mask) == 0U ? 1U : 0U;
    if (physical_river_coverage[card] != expected_count) {
      return Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
  }
  return !expected.empty()
             ? Result<std::vector<std::uint64_t>, HuPreflopError>::success(std::move(expected))
             : Result<std::vector<std::uint64_t>, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

bool valid_flop_boundary_value_rows(const std::vector<HuPreflopFlopBoundaryValue> &rows,
                                    const std::array<CardId, 3> &flop) {
  if (rows.size() != live_combos_per_flop) {
    return false;
  }
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(flop);
  std::array<bool, 630U> observed{};
  for (const auto &value : rows) {
    if (value.opponent_combo >= masks.size() || observed[value.opponent_combo] ||
        (masks[value.opponent_combo] & board_mask) != 0U ||
        !std::isfinite(value.counterfactual_reach) || value.counterfactual_reach < 0.0 ||
        !std::isfinite(value.blueprint_counterfactual_value_antes) ||
        value.positive_reach != (value.counterfactual_reach > 0.0) ||
        (!value.positive_reach && value.blueprint_counterfactual_value_antes != 0.0)) {
      return false;
    }
    observed[value.opponent_combo] = true;
  }
  return std::count(observed.begin(), observed.end(), true) == live_combos_per_flop;
}

bool valid_river_root_boundary_components(const HuPreflopRiverRootBoundary &boundary) {
  if (!valid_flop_boundary_value_rows(boundary.values, boundary.flop)) {
    return false;
  }
  if (boundary.value_mode == HuPreflopContinuationValueMode::AverageStrategy) {
    return boundary.best_response_turn_components.empty();
  }
  if (boundary.value_mode != HuPreflopContinuationValueMode::ExactBestResponse) {
    return false;
  }

  const auto lifts = ordered_runout_orbit_lifts(boundary.flop, boundary.turn, boundary.river);
  if (!lifts) {
    return false;
  }
  std::array<bool, 36U> expected_turns{};
  for (const auto &permutation : lifts.value()) {
    const auto turn = static_cast<std::uint8_t>((boundary.turn.value() / 4U) * 4U +
                                                permutation[boundary.turn.value() % 4U]);
    if (turn >= expected_turns.size()) {
      return false;
    }
    expected_turns[turn] = true;
  }
  if (boundary.best_response_turn_components.size() !=
      static_cast<std::size_t>(std::count(expected_turns.begin(), expected_turns.end(), true))) {
    return false;
  }

  std::array<bool, 36U> observed_turns{};
  std::array<double, 630U> component_reach{};
  std::array<double, 630U> component_utility{};
  for (const auto &component : boundary.best_response_turn_components) {
    if (component.turn.value() >= observed_turns.size() ||
        !expected_turns[component.turn.value()] || observed_turns[component.turn.value()] ||
        !valid_flop_boundary_value_rows(component.values, boundary.flop)) {
      return false;
    }
    observed_turns[component.turn.value()] = true;
    for (const auto &value : component.values) {
      component_reach[value.opponent_combo] += value.counterfactual_reach;
      component_utility[value.opponent_combo] +=
          value.counterfactual_reach * value.blueprint_counterfactual_value_antes;
    }
  }
  for (const auto &value : boundary.values) {
    const auto aggregate_utility =
        value.counterfactual_reach * value.blueprint_counterfactual_value_antes;
    const auto reach_tolerance =
        1.0e-10 * std::max({1.0, std::abs(value.counterfactual_reach),
                            std::abs(component_reach[value.opponent_combo])});
    const auto utility_tolerance =
        1.0e-10 * std::max({1.0, std::abs(aggregate_utility),
                            std::abs(component_utility[value.opponent_combo])});
    if (std::abs(component_reach[value.opponent_combo] - value.counterfactual_reach) >
            reach_tolerance ||
        std::abs(component_utility[value.opponent_combo] - aggregate_utility) > utility_tolerance) {
      return false;
    }
  }
  return true;
}

Result<std::array<double, 2>, HuPreflopError>
terminal_payoff_antes(const PublicState &state, const RakeConfig &rake,
                      const std::uint8_t winner_mask) {
  const auto settlement = settle_terminal(state, rake, winner_mask);
  if (!settlement) {
    return Result<std::array<double, 2>, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  return Result<std::array<double, 2>, HuPreflopError>::success(
      {static_cast<double>(settlement.value().payoff_units[0]) /
           static_cast<double>(Money::units_per_ante),
       static_cast<double>(settlement.value().payoff_units[1]) /
           static_cast<double>(Money::units_per_ante)});
}

Result<bool, HuPreflopError> accumulate_fold_terminal_path(
    const HuPreflopPostflopEntryReach &entry, const ReplayedTerminalPath &path,
    const std::uint32_t flop_multiplicity,
    std::array<std::array<HuPreflopCompensatedCounterfactualValue, 630U>, 2> &values,
    const RakeConfig &rake) {
  const auto payoff = terminal_payoff_antes(path.state, rake, 0U);
  if (!payoff) {
    return Result<bool, HuPreflopError>::failure(payoff.error());
  }
  const auto combos = all_combos();
  const auto masks = combo_masks();
  const auto board_mask = path.state.board_mask;
  const double remaining_runouts = path.state.street == Street::Flop ? 812.0 : 28.0;
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    const auto opponent = static_cast<std::uint8_t>(1U - resolver);
    std::array<double, 36U> resolver_mass_by_card{};
    std::array<double, 630U> resolver_mass{};
    double resolver_total = 0.0;
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & board_mask) != 0U) {
        continue;
      }
      const auto mass =
          entry.own_sequence_reach[resolver][combo] * path.postflop_action_reach[resolver][combo];
      if (!std::isfinite(mass) || mass < 0.0) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
      }
      resolver_mass[combo] = mass;
      resolver_total += mass;
      resolver_mass_by_card[combos[combo].first.value()] += mass;
      resolver_mass_by_card[combos[combo].second.value()] += mass;
    }
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & board_mask) != 0U) {
        continue;
      }
      const auto target_action_reach = path.postflop_action_reach[opponent][combo];
      const auto compatible_mass =
          resolver_total - resolver_mass_by_card[combos[combo].first.value()] -
          resolver_mass_by_card[combos[combo].second.value()] + resolver_mass[combo];
      const auto weighted_reach = compatible_mass * target_action_reach * remaining_runouts *
                                  static_cast<double>(flop_multiplicity);
      const auto weighted_utility = weighted_reach * payoff.value()[opponent];
      if (!std::isfinite(weighted_reach) || weighted_reach < 0.0 ||
          !std::isfinite(weighted_utility)) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
      }
      compensated_add(weighted_reach, values[resolver][combo].weighted_reach_sum,
                      values[resolver][combo].weighted_reach_compensation);
      compensated_add(weighted_utility, values[resolver][combo].weighted_utility_sum_antes,
                      values[resolver][combo].weighted_utility_compensation_antes);
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<bool, HuPreflopError> accumulate_all_in_terminal_path(
    const HuPreflopPostflopEntryReach &entry, const ReplayedTerminalPath &path,
    const std::array<CardId, 3> &flop, const std::optional<CardId> fixed_turn,
    const std::uint32_t flop_multiplicity,
    std::array<std::array<HuPreflopCompensatedCounterfactualValue, 630U>, 2> &values,
    const RakeConfig &rake) {
  const auto combos = all_combos();
  const auto masks = combo_masks();
  std::vector<std::size_t> positive_first;
  std::vector<std::size_t> positive_second;
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    if ((masks[combo] & path.state.board_mask) != 0U) {
      continue;
    }
    if (path.postflop_action_reach[0][combo] > 0.0) {
      positive_first.push_back(combo);
    }
    if (path.postflop_action_reach[1][combo] > 0.0) {
      positive_second.push_back(combo);
    }
  }
  bool has_compatible_pair = false;
  for (const auto first : positive_first) {
    for (const auto second : positive_second) {
      if ((masks[first] & masks[second]) == 0U &&
          (entry.own_sequence_reach[0][first] > 0.0 || entry.own_sequence_reach[1][second] > 0.0)) {
        has_compatible_pair = true;
        break;
      }
    }
    if (has_compatible_pair) {
      break;
    }
  }
  if (!has_compatible_pair) {
    return Result<bool, HuPreflopError>::success(true);
  }

  const auto payoff_player_zero = terminal_payoff_antes(path.state, rake, 0b01U);
  const auto payoff_player_one = terminal_payoff_antes(path.state, rake, 0b10U);
  const auto payoff_tie = terminal_payoff_antes(path.state, rake, 0b11U);
  if (!payoff_player_zero || !payoff_player_one || !payoff_tie) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  const auto flop_board_mask = flop_mask(flop);
  const auto evaluate_complete_board = [&](const CardId turn,
                                           const CardId river) -> Result<bool, HuPreflopError> {
    const auto board_mask = flop_board_mask | turn.mask() | river.mask();
    std::array<HandValue, 630U> hand_values{};
    std::array<bool, 630U> live{};
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & board_mask) != 0U || (path.postflop_action_reach[0][combo] == 0.0 &&
                                                path.postflop_action_reach[1][combo] == 0.0)) {
        continue;
      }
      const std::array<CardId, 7> cards{
          combos[combo].first, combos[combo].second, flop[0], flop[1], flop[2], turn, river};
      const auto evaluated = evaluate_seven(cards);
      if (!evaluated) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::EquityFailure);
      }
      hand_values[combo] = evaluated.value();
      live[combo] = true;
    }
    for (std::size_t first = 0U; first < masks.size(); ++first) {
      const auto first_action = path.postflop_action_reach[0][first];
      if (!live[first] || first_action == 0.0) {
        continue;
      }
      for (std::size_t second = 0U; second < masks.size(); ++second) {
        const auto second_action = path.postflop_action_reach[1][second];
        if (!live[second] || second_action == 0.0 || (masks[first] & masks[second]) != 0U) {
          continue;
        }
        const auto winner_mask = hand_values[first] > hand_values[second]   ? 0b01U
                                 : hand_values[second] > hand_values[first] ? 0b10U
                                                                            : 0b11U;
        const auto &payoff = winner_mask == 0b01U   ? payoff_player_zero.value()
                             : winner_mask == 0b10U ? payoff_player_one.value()
                                                    : payoff_tie.value();
        const auto common_action =
            first_action * second_action * static_cast<double>(flop_multiplicity);
        const auto reach_resolver_zero = entry.own_sequence_reach[0][first] * common_action;
        const auto reach_resolver_one = entry.own_sequence_reach[1][second] * common_action;
        if (!std::isfinite(reach_resolver_zero) || reach_resolver_zero < 0.0 ||
            !std::isfinite(reach_resolver_one) || reach_resolver_one < 0.0) {
          return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
        }
        compensated_add(reach_resolver_zero, values[0][second].weighted_reach_sum,
                        values[0][second].weighted_reach_compensation);
        compensated_add(reach_resolver_zero * payoff[1],
                        values[0][second].weighted_utility_sum_antes,
                        values[0][second].weighted_utility_compensation_antes);
        compensated_add(reach_resolver_one, values[1][first].weighted_reach_sum,
                        values[1][first].weighted_reach_compensation);
        compensated_add(reach_resolver_one * payoff[0], values[1][first].weighted_utility_sum_antes,
                        values[1][first].weighted_utility_compensation_antes);
      }
    }
    return Result<bool, HuPreflopError>::success(true);
  };

  if (fixed_turn.has_value()) {
    for (const auto river : short_deck()) {
      if ((river.mask() & (flop_board_mask | fixed_turn->mask())) != 0U) {
        continue;
      }
      const auto accumulated = evaluate_complete_board(*fixed_turn, river);
      if (!accumulated) {
        return accumulated;
      }
    }
  } else {
    for (const auto turn : short_deck()) {
      if ((turn.mask() & flop_board_mask) != 0U) {
        continue;
      }
      for (const auto river : short_deck()) {
        if ((river.mask() & (flop_board_mask | turn.mask())) != 0U) {
          continue;
        }
        const auto accumulated = evaluate_complete_board(turn, river);
        if (!accumulated) {
          return accumulated;
        }
      }
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

bool valid_upper_terminal_contribution_payload(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopUpperStreetTerminalContribution &contribution) {
  return contribution.major == HuPreflopUpperStreetTerminalContribution::format_major &&
         contribution.minor == HuPreflopUpperStreetTerminalContribution::format_minor &&
         contribution.tree_fingerprint == tree.fingerprint &&
         contribution.blueprint_fingerprint == decomposition.blueprint_fingerprint &&
         !contribution.continuation_checkpoint_fingerprint.empty() &&
         contribution.blueprint_iterations > 0U && valid_flop(contribution.flop) &&
         contribution.task_span_index ==
             whole_game_task_index(decomposition, contribution.entry_node, contribution.flop) &&
         (contribution.terminal_street == Street::Flop ||
          contribution.terminal_street == Street::Turn) &&
         (contribution.terminal_status == HandStatus::Folded ||
          contribution.terminal_status == HandStatus::AllInRunout) &&
         !contribution.terminal_history_fingerprint.empty() &&
         valid_task_value_rows(contribution.resolver_values, contribution.flop) &&
         !contribution.fingerprint.empty() &&
         contribution.fingerprint == upper_terminal_contribution_fingerprint(contribution);
}

bool valid_upper_best_response_terminal_contribution_payload(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog,
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution) {
  if (contribution.major != HuPreflopUpperStreetBestResponseTerminalContribution::format_major ||
      contribution.minor != HuPreflopUpperStreetBestResponseTerminalContribution::format_minor ||
      contribution.tree_fingerprint != tree.fingerprint ||
      contribution.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      contribution.root_catalog_fingerprint != catalog.fingerprint ||
      contribution.continuation_fingerprint.empty() || contribution.blueprint_iterations == 0U ||
      contribution.responding_player > 1U || !valid_flop(contribution.flop) ||
      contribution.task_span_index !=
          whole_game_task_index(decomposition, contribution.entry_node, contribution.flop) ||
      contribution.task_span_index >= catalog.task_spans.size() ||
      catalog.task_spans[contribution.task_span_index].entry_node != contribution.entry_node ||
      (contribution.terminal_street != Street::Flop &&
       contribution.terminal_street != Street::Turn) ||
      contribution.has_turn != (contribution.terminal_street == Street::Turn) ||
      (contribution.terminal_status != HandStatus::Folded &&
       contribution.terminal_status != HandStatus::AllInRunout) ||
      contribution.terminal_history_fingerprint.empty() ||
      !valid_best_response_combo_value_rows(contribution.values, contribution.flop) ||
      contribution.fingerprint.empty() ||
      contribution.fingerprint !=
          upper_best_response_terminal_contribution_fingerprint(contribution)) {
    return false;
  }
  if (!contribution.has_turn) {
    if (!contribution.turn_group_fingerprint.empty() || !contribution.turn_components.empty()) {
      return false;
    }
    return true;
  }

  const auto groups = enumerate_hu_preflop_river_turn_groups(catalog, contribution.task_span_index);
  if (!groups || std::ranges::none_of(groups.value(), [&](const auto &group) {
        return group.fingerprint == contribution.turn_group_fingerprint &&
               group.turn == contribution.turn;
      })) {
    return false;
  }
  const auto lifts = turn_orbit_lifts(contribution.flop, contribution.turn);
  if (!lifts || contribution.turn_components.size() != lifts.value().size()) {
    return false;
  }

  const auto board_mask = flop_mask(contribution.flop);
  std::array<bool, 36U> expected_turns{};
  for (const auto &permutation : lifts.value()) {
    const auto physical_turn = static_cast<std::uint8_t>(
        (contribution.turn.value() / 4U) * 4U + permutation[contribution.turn.value() % 4U]);
    if (physical_turn >= expected_turns.size() || expected_turns[physical_turn] ||
        (CardId::from_index(physical_turn).value().mask() & board_mask) != 0U) {
      return false;
    }
    expected_turns[physical_turn] = true;
  }

  std::array<bool, 36U> observed_turns{};
  std::array<double, 630U> component_reach{};
  std::array<double, 630U> component_utility{};
  for (const auto &component : contribution.turn_components) {
    if (component.turn.value() >= observed_turns.size() ||
        !expected_turns[component.turn.value()] || observed_turns[component.turn.value()] ||
        !valid_best_response_combo_value_rows(component.values, contribution.flop)) {
      return false;
    }
    observed_turns[component.turn.value()] = true;
    for (const auto &value : component.values) {
      component_reach[value.responding_combo] += value.weighted_counterfactual_reach;
      component_utility[value.responding_combo] += value.weighted_counterfactual_utility_antes;
    }
  }
  if (observed_turns != expected_turns) {
    return false;
  }
  for (const auto &value : contribution.values) {
    const auto reach_tolerance =
        1.0e-10 * std::max({1.0, std::abs(component_reach[value.responding_combo]),
                            std::abs(value.weighted_counterfactual_reach)});
    const auto utility_tolerance =
        1.0e-10 * std::max({1.0, std::abs(component_utility[value.responding_combo]),
                            std::abs(value.weighted_counterfactual_utility_antes)});
    if (std::abs(component_reach[value.responding_combo] - value.weighted_counterfactual_reach) >
            reach_tolerance ||
        std::abs(component_utility[value.responding_combo] -
                 value.weighted_counterfactual_utility_antes) > utility_tolerance) {
      return false;
    }
  }
  return true;
}

bool valid_flop_task_accumulator_payload(const HuPreflopTree &tree,
                                         const HuPreflopDecompositionPlan &decomposition,
                                         const HuPreflopFlopTaskAccumulator &accumulator) {
  if (accumulator.major != HuPreflopFlopTaskAccumulator::format_major ||
      accumulator.minor != HuPreflopFlopTaskAccumulator::format_minor ||
      accumulator.tree_fingerprint != tree.fingerprint ||
      accumulator.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      accumulator.river_aggregate_fingerprint.empty() ||
      accumulator.continuation_checkpoint_fingerprint.empty() ||
      accumulator.blueprint_iterations == 0U || !valid_flop(accumulator.flop) ||
      accumulator.task_span_index !=
          whole_game_task_index(decomposition, accumulator.entry_node, accumulator.flop) ||
      accumulator.expected_terminal_fingerprints.empty() ||
      accumulator.next_terminal_ordinal > accumulator.expected_terminal_fingerprints.size() ||
      accumulator.complete != (accumulator.next_terminal_ordinal ==
                               accumulator.expected_terminal_fingerprints.size()) ||
      accumulator.numeric_state_hash != flop_task_numeric_state_hash(accumulator) ||
      accumulator.contribution_chain_fingerprint.empty() || accumulator.fingerprint.empty() ||
      accumulator.fingerprint != flop_task_accumulator_fingerprint(accumulator) ||
      tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition)) {
    return false;
  }
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(accumulator.flop);
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    for (const auto &resolver : accumulator.values) {
      const auto &value = resolver[combo];
      if (!std::isfinite(value.weighted_reach_sum) ||
          !std::isfinite(value.weighted_reach_compensation) ||
          !std::isfinite(value.weighted_utility_sum_antes) ||
          !std::isfinite(value.weighted_utility_compensation_antes) ||
          ((masks[combo] & board_mask) != 0U &&
           (value.weighted_reach_sum != 0.0 || value.weighted_reach_compensation != 0.0 ||
            value.weighted_utility_sum_antes != 0.0 ||
            value.weighted_utility_compensation_antes != 0.0))) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const std::uint64_t task_span_index, const std::array<CardId, 3> &requested_flop,
    const std::uint64_t terminal_ordinal, const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPostflopActionProbabilityProvider &probability_provider,
    const std::uint64_t blueprint_iterations, std::string continuation_checkpoint_fingerprint) {
  const auto flop = canonical_flop(requested_flop);
  if (!probability_provider || blueprint_iterations == 0U ||
      continuation_checkpoint_fingerprint.empty() ||
      tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      decomposition.canonical_flops == 0U ||
      task_span_index >= decomposition.canonical_public_flop_roots) {
    return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto entry_index = task_span_index / decomposition.canonical_flops;
  if (entry_index >= decomposition.entries.size()) {
    return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto *entry = &decomposition.entries[entry_index];
  const auto expected_task = whole_game_task_index(decomposition, entry->entry_node, flop);
  const auto flop_catalog =
      std::ranges::find(decomposition.canonical_flop_catalog, flop, &HuPreflopCanonicalFlop::cards);
  if (expected_task != task_span_index ||
      flop_catalog == decomposition.canonical_flop_catalog.end() ||
      terminal_shape.entry_node != entry->entry_node ||
      terminal_shape.terminal_ordinal != terminal_ordinal) {
    return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &shape = terminal_shape;
  std::array<std::array<HuPreflopCompensatedCounterfactualValue, 630U>, 2> accumulated{};
  const auto evaluate_path = [&](const std::optional<CardId> turn) -> Result<bool, HuPreflopError> {
    const auto path =
        replay_terminal_path_with_cards(tree, shape, flop, turn, probability_provider);
    if (!path) {
      return Result<bool, HuPreflopError>::failure(path.error());
    }
    return shape.state.status == HandStatus::Folded
               ? accumulate_fold_terminal_path(*entry, path.value(),
                                               flop_catalog->physical_outcome_count, accumulated,
                                               tree.config.rake)
               : accumulate_all_in_terminal_path(*entry, path.value(), flop, turn,
                                                 flop_catalog->physical_outcome_count, accumulated,
                                                 tree.config.rake);
  };
  if (shape.street == Street::Flop) {
    const auto evaluated = evaluate_path(std::nullopt);
    if (!evaluated) {
      return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
          evaluated.error());
    }
  } else {
    for (const auto turn : short_deck()) {
      if ((turn.mask() & flop_mask(flop)) != 0U) {
        continue;
      }
      const auto evaluated = evaluate_path(turn);
      if (!evaluated) {
        return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
            evaluated.error());
      }
    }
  }

  HuPreflopUpperStreetTerminalContribution result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  result.continuation_checkpoint_fingerprint = std::move(continuation_checkpoint_fingerprint);
  result.blueprint_iterations = blueprint_iterations;
  result.task_span_index = task_span_index;
  result.entry_node = entry->entry_node;
  result.flop = flop;
  result.terminal_ordinal = terminal_ordinal;
  result.terminal_street = shape.street;
  result.terminal_status = shape.state.status;
  result.terminal_history_fingerprint = shape.history_fingerprint;
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(flop);
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    auto &output = result.resolver_values[resolver];
    output.reserve(live_combos_per_flop);
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & board_mask) != 0U) {
        continue;
      }
      const auto &source = accumulated[resolver][combo];
      const auto reach = source.weighted_reach_sum + source.weighted_reach_compensation;
      const auto utility =
          source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
      if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
          (reach == 0.0 && utility != 0.0)) {
        return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      output.push_back({static_cast<ComboId>(combo), reach, utility,
                        reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
    }
  }
  result.fingerprint = upper_terminal_contribution_fingerprint(result);
  return valid_upper_terminal_contribution_payload(tree, decomposition, result)
             ? Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::success(
                   std::move(result))
             : Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const std::uint64_t task_span_index,
    const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopRiverTurnGroup *turn_group,
    const HuPostflopActionProbabilityProvider &probability_provider,
    const std::uint8_t responding_player, const std::uint64_t blueprint_iterations,
    std::string continuation_fingerprint) {
  if (!probability_provider || responding_player > 1U || blueprint_iterations == 0U ||
      continuation_fingerprint.empty() || tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      catalog.tree_fingerprint != tree.fingerprint ||
      catalog.decomposition_plan_fingerprint != decomposition.fingerprint ||
      task_span_index >= catalog.task_spans.size()) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &task = catalog.task_spans[task_span_index];
  const auto &flop = catalog.canonical_boards[task.first_board_index].flop;
  if (terminal_shape.entry_node != task.entry_node ||
      (terminal_shape.street == Street::Flop && turn_group != nullptr) ||
      (terminal_shape.street == Street::Turn && turn_group == nullptr) ||
      (terminal_shape.street != Street::Flop && terminal_shape.street != Street::Turn)) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto flop_catalog =
      std::ranges::find(decomposition.canonical_flop_catalog, flop, &HuPreflopCanonicalFlop::cards);
  if (flop_catalog == decomposition.canonical_flop_catalog.end()) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  if (turn_group != nullptr) {
    const auto spans = enumerate_hu_preflop_river_turn_group_resolver_spans(catalog, *turn_group);
    if (!spans || turn_group->task_span_index != task_span_index ||
        turn_group->entry_node != task.entry_node || turn_group->flop != flop) {
      return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
  }

  const HuPostflopActionProbabilityProvider opponent_probability_provider =
      [&](const PublicState &state, const std::span<const Action> prefix, const Action &action,
          const ComboId combo) {
        return state.player_to_act == responding_player
                   ? Result<double, HuPreflopError>::success(1.0)
                   : probability_provider(state, prefix, action, combo);
      };
  using AccumulatorMatrix =
      std::array<std::array<HuPreflopCompensatedCounterfactualValue, 630U>, 2>;
  std::unique_ptr<AccumulatorMatrix> accumulated;
  try {
    accumulated = std::make_unique<AccumulatorMatrix>();
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
  const auto accumulate_path = [&](const std::optional<CardId> physical_turn,
                                   AccumulatorMatrix &target) -> Result<bool, HuPreflopError> {
    const auto path = replay_terminal_path_with_cards(tree, terminal_shape, flop, physical_turn,
                                                      opponent_probability_provider);
    if (!path) {
      return Result<bool, HuPreflopError>::failure(path.error());
    }
    const auto *entry = find_entry(decomposition, task.entry_node);
    if (entry == nullptr) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    return terminal_shape.state.status == HandStatus::Folded
               ? accumulate_fold_terminal_path(*entry, path.value(),
                                               flop_catalog->physical_outcome_count, target,
                                               tree.config.rake)
               : accumulate_all_in_terminal_path(*entry, path.value(), flop, physical_turn,
                                                 flop_catalog->physical_outcome_count, target,
                                                 tree.config.rake);
  };
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(flop);
  const auto target_resolver = static_cast<std::uint8_t>(1U - responding_player);
  std::vector<HuPreflopBestResponseTurnComponent> turn_components;

  if (turn_group == nullptr) {
    const auto evaluated = accumulate_path(std::nullopt, *accumulated);
    if (!evaluated) {
      return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
          evaluated.error());
    }
  } else {
    const auto lifts = turn_orbit_lifts(flop, turn_group->turn);
    std::uint64_t expected_physical_outcomes = 0U;
    if (!lifts ||
        !checked_multiply(static_cast<std::uint64_t>(lifts.value().size()), 32U,
                          expected_physical_outcomes) ||
        !checked_multiply(expected_physical_outcomes,
                          static_cast<std::uint64_t>(flop_catalog->physical_outcome_count),
                          expected_physical_outcomes) ||
        expected_physical_outcomes != turn_group->physical_public_outcome_count) {
      return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    turn_components.reserve(lifts.value().size());
    for (const auto &permutation : lifts.value()) {
      const auto turn_index = static_cast<std::uint8_t>((turn_group->turn.value() / 4U) * 4U +
                                                        permutation[turn_group->turn.value() % 4U]);
      const auto physical_turn = CardId::from_index(turn_index);
      if (!physical_turn) {
        return Result<HuPreflopUpperStreetBestResponseTerminalContribution,
                      HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      std::unique_ptr<AccumulatorMatrix> physical_values;
      try {
        physical_values = std::make_unique<AccumulatorMatrix>();
      } catch (const std::bad_alloc &) {
        return Result<HuPreflopUpperStreetBestResponseTerminalContribution,
                      HuPreflopError>::failure(HuPreflopError::MemoryFailure);
      }
      const auto evaluated = accumulate_path(physical_turn.value(), *physical_values);
      if (!evaluated) {
        return Result<HuPreflopUpperStreetBestResponseTerminalContribution,
                      HuPreflopError>::failure(evaluated.error());
      }
      HuPreflopBestResponseTurnComponent component;
      component.turn = physical_turn.value();
      component.values.reserve(live_combos_per_flop);
      for (std::size_t combo = 0U; combo < (*physical_values)[target_resolver].size(); ++combo) {
        const auto &source = (*physical_values)[target_resolver][combo];
        const auto reach = source.weighted_reach_sum + source.weighted_reach_compensation;
        const auto utility =
            source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
        if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
            (reach == 0.0 && utility != 0.0)) {
          return Result<HuPreflopUpperStreetBestResponseTerminalContribution,
                        HuPreflopError>::failure(HuPreflopError::NumericalFailure);
        }
        if ((masks[combo] & board_mask) == 0U) {
          component.values.push_back({static_cast<ComboId>(combo), reach, utility,
                                      reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
        }
        auto &destination = (*accumulated)[target_resolver][combo];
        compensated_add(reach, destination.weighted_reach_sum,
                        destination.weighted_reach_compensation);
        compensated_add(utility, destination.weighted_utility_sum_antes,
                        destination.weighted_utility_compensation_antes);
      }
      turn_components.push_back(std::move(component));
    }
  }

  HuPreflopUpperStreetBestResponseTerminalContribution result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  result.root_catalog_fingerprint = catalog.fingerprint;
  result.turn_group_fingerprint = turn_group != nullptr ? turn_group->fingerprint : "";
  result.continuation_fingerprint = std::move(continuation_fingerprint);
  result.blueprint_iterations = blueprint_iterations;
  result.task_span_index = task_span_index;
  result.entry_node = task.entry_node;
  result.flop = flop;
  result.turn = turn_group != nullptr ? turn_group->turn : CardId{};
  result.has_turn = turn_group != nullptr;
  result.responding_player = responding_player;
  result.terminal_ordinal = terminal_shape.terminal_ordinal;
  result.terminal_street = terminal_shape.street;
  result.terminal_status = terminal_shape.state.status;
  result.terminal_history_fingerprint = terminal_shape.history_fingerprint;
  result.turn_components = std::move(turn_components);
  result.values.reserve(live_combos_per_flop);
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    if ((masks[combo] & board_mask) != 0U) {
      continue;
    }
    const auto &source = (*accumulated)[target_resolver][combo];
    const auto reach = source.weighted_reach_sum + source.weighted_reach_compensation;
    const auto utility =
        source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
    if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
        (reach == 0.0 && utility != 0.0)) {
      return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    result.values.push_back({static_cast<ComboId>(combo), reach, utility,
                             reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
  }
  result.fingerprint = upper_best_response_terminal_contribution_fingerprint(result);
  return valid_upper_best_response_terminal_contribution_payload(tree, decomposition, catalog,
                                                                 result)
             ? Result<HuPreflopUpperStreetBestResponseTerminalContribution,
                      HuPreflopError>::success(std::move(result))
             : Result<HuPreflopUpperStreetBestResponseTerminalContribution,
                      HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
select_hu_preflop_upper_street_best_response_leaf(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog,
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution,
    const HuPreflopBestResponseLeafQuery &query) {
  const auto valid_contribution =
      validate_hu_preflop_upper_street_best_response_terminal_contribution(tree, decomposition,
                                                                           catalog, contribution);
  if (!valid_contribution || query.kind != HuPreflopBestResponseLeafKind::UpperStreetTerminal ||
      query.task_span_index != contribution.task_span_index ||
      query.entry_node != contribution.entry_node || query.flop != contribution.flop ||
      query.responding_player != contribution.responding_player ||
      query.state.street != contribution.terminal_street ||
      query.state.status != contribution.terminal_status ||
      query.history_fingerprint != contribution.terminal_history_fingerprint) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        valid_contribution ? HuPreflopError::InvalidConfiguration : valid_contribution.error());
  }
  const auto shapes = enumerate_hu_postflop_upper_street_terminal_shapes(tree);
  if (!shapes) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        shapes.error());
  }
  const auto shape = std::ranges::find_if(shapes.value(), [&](const auto &candidate) {
    return candidate.entry_node == contribution.entry_node &&
           candidate.terminal_ordinal == contribution.terminal_ordinal;
  });
  if (shape == shapes.value().end() || shape->action_history != query.action_history) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  auto expected_state = shape->state;
  expected_state.board_mask = flop_mask(query.flop) | (query.has_turn ? query.turn.mask() : 0U);
  if (query.state != expected_state || query.has_turn != (query.state.street == Street::Turn)) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  if (!query.has_turn) {
    return valid_best_response_leaf_values(query, contribution.values)
               ? Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::success(
                     contribution.values)
               : Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
                     HuPreflopError::IntegrityFailure);
  }
  const auto component = std::ranges::find(contribution.turn_components, query.turn,
                                           &HuPreflopBestResponseTurnComponent::turn);
  return component != contribution.turn_components.end() &&
                 valid_best_response_leaf_values(query, component->values)
             ? Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::success(
                   component->values)
             : Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
reduce_hu_preflop_best_response_action_values(
    const std::span<const std::vector<HuPreflopBestResponseComboValue>> action_children,
    const std::array<CardId, 3> &flop, const bool responding_player_acts) {
  if (action_children.empty() || !valid_flop(flop)) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  using ComboValue = HuPreflopBestResponseComboValue;
  std::vector<std::array<const ComboValue *, 630U>> indexed_children;
  indexed_children.reserve(action_children.size());
  for (const auto &child : action_children) {
    if (!valid_best_response_combo_value_rows(child, flop)) {
      return Result<std::vector<ComboValue>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    std::array<const ComboValue *, 630U> indexed{};
    for (const auto &value : child) {
      indexed[value.responding_combo] = &value;
    }
    indexed_children.push_back(indexed);
  }

  const auto masks = combo_masks();
  const auto board_mask = flop_mask(flop);
  std::vector<ComboValue> result;
  result.reserve(live_combos_per_flop);
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    if ((masks[combo] & board_mask) != 0U) {
      continue;
    }
    const auto *first = indexed_children.front()[combo];
    if (first == nullptr) {
      return Result<std::vector<ComboValue>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }

    double reach = 0.0;
    double utility = 0.0;
    if (responding_player_acts) {
      reach = first->weighted_counterfactual_reach;
      utility = first->weighted_counterfactual_utility_antes;
      for (std::size_t child_index = 1U; child_index < indexed_children.size(); ++child_index) {
        const auto *candidate = indexed_children[child_index][combo];
        if (candidate == nullptr) {
          return Result<std::vector<ComboValue>, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        const auto candidate_reach = candidate->weighted_counterfactual_reach;
        const auto reach_tolerance =
            probability_tolerance * std::max({1.0, std::abs(reach), std::abs(candidate_reach)});
        const auto zero_reach_mismatch = (reach == 0.0) != (candidate_reach == 0.0);
        if (zero_reach_mismatch || std::abs(candidate_reach - reach) > reach_tolerance) {
          return Result<std::vector<ComboValue>, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        if (candidate->weighted_counterfactual_utility_antes > utility) {
          utility = candidate->weighted_counterfactual_utility_antes;
        }
      }
    } else {
      double reach_compensation = 0.0;
      double utility_compensation = 0.0;
      for (const auto &child : indexed_children) {
        const auto *candidate = child[combo];
        if (candidate == nullptr) {
          return Result<std::vector<ComboValue>, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        compensated_add(candidate->weighted_counterfactual_reach, reach, reach_compensation);
        compensated_add(candidate->weighted_counterfactual_utility_antes, utility,
                        utility_compensation);
      }
      reach += reach_compensation;
      utility += utility_compensation;
    }

    if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
        (reach == 0.0 && utility != 0.0)) {
      return Result<std::vector<ComboValue>, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    result.push_back({static_cast<ComboId>(combo), reach, utility,
                      reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
  }

  return valid_best_response_combo_value_rows(result, flop)
             ? Result<std::vector<ComboValue>, HuPreflopError>::success(std::move(result))
             : Result<std::vector<ComboValue>, HuPreflopError>::failure(
                   HuPreflopError::NumericalFailure);
}

std::string fingerprint_hu_preflop_best_response_task_evaluation(
    const HuPreflopBestResponseTaskEvaluation &evaluation) {
  return best_response_task_evaluation_fingerprint(evaluation);
}

Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError> evaluate_hu_preflop_best_response_task(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const std::uint64_t task_span_index, const HuPreflopBestResponseLeafProvider &leaf_provider,
    std::string continuation_fingerprint, const std::uint8_t responding_player,
    const std::uint64_t blueprint_iterations) {
  if (!leaf_provider || continuation_fingerprint.empty() || responding_player > 1U ||
      blueprint_iterations == 0U || tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      decomposition.canonical_flops == 0U ||
      decomposition.canonical_flops != decomposition.canonical_flop_catalog.size() ||
      task_span_index >= decomposition.canonical_public_flop_roots) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto entry_index = task_span_index / decomposition.canonical_flops;
  const auto flop_index = task_span_index % decomposition.canonical_flops;
  if (entry_index >= decomposition.entries.size() ||
      flop_index >= decomposition.canonical_flop_catalog.size()) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto &entry = decomposition.entries[entry_index];
  const auto &flop = decomposition.canonical_flop_catalog[flop_index].cards;
  if (entry.entry_node >= tree.nodes.size() ||
      tree.nodes[entry.entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      tree.nodes[entry.entry_node].state.street != Street::Preflop ||
      tree.nodes[entry.entry_node].state.status != HandStatus::StreetComplete) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  HuPreflopBestResponseTaskEvaluation result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  result.continuation_fingerprint = std::move(continuation_fingerprint);
  result.blueprint_iterations = blueprint_iterations;
  result.task_span_index = task_span_index;
  result.entry_node = entry.entry_node;
  result.flop = flop;
  result.responding_player = responding_player;

  const auto action_config = hu_preflop_postflop_action_config(tree);
  const auto flop_board_mask = flop_mask(flop);
  std::vector<Action> action_history;
  const auto increment = [](std::uint64_t &counter) {
    if (counter == std::numeric_limits<std::uint64_t>::max()) {
      return false;
    }
    ++counter;
    return true;
  };
  using ComboValues = std::vector<HuPreflopBestResponseComboValue>;
  std::function<Result<ComboValues, HuPreflopError>(const PublicState &, std::optional<CardId>,
                                                    std::uint64_t, std::uint64_t)>
      visit;
  visit = [&](const PublicState &state, const std::optional<CardId> physical_turn,
              const std::uint64_t history,
              const std::uint64_t depth) -> Result<ComboValues, HuPreflopError> {
    result.maximum_recursion_depth = std::max(result.maximum_recursion_depth, depth);
    const auto request_leaf =
        [&](const HuPreflopBestResponseLeafKind kind, const PublicState &leaf_state,
            const std::uint64_t leaf_history) -> Result<ComboValues, HuPreflopError> {
      HuPreflopBestResponseLeafQuery query;
      query.kind = kind;
      query.task_span_index = task_span_index;
      query.entry_node = entry.entry_node;
      query.flop = flop;
      query.turn = physical_turn.value_or(CardId{});
      query.has_turn = physical_turn.has_value();
      query.responding_player = responding_player;
      query.state = leaf_state;
      query.action_history = action_history;
      query.history_fingerprint = "fnv1a64:" + hex64(leaf_history);
      auto values = leaf_provider(query);
      if (!values) {
        return Result<ComboValues, HuPreflopError>::failure(values.error());
      }
      if (!valid_best_response_leaf_values(query, values.value())) {
        return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      auto &counter = kind == HuPreflopBestResponseLeafKind::UpperStreetTerminal
                          ? result.upper_terminal_leaves_visited
                          : result.river_continuation_leaves_visited;
      if (!increment(counter)) {
        return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::CountOverflow);
      }
      return Result<ComboValues, HuPreflopError>::success(std::move(values.value()));
    };

    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout) {
      if ((state.street == Street::Flop) != !physical_turn.has_value() ||
          (state.street != Street::Flop && state.street != Street::Turn)) {
        return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      return request_leaf(HuPreflopBestResponseLeafKind::UpperStreetTerminal, state, history);
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_street(state);
      if (!advanced) {
        return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::GameFailure);
      }
      const auto next_history = fnv1a(
          "street:" + std::to_string(static_cast<unsigned>(advanced.value().street)), history);
      if (state.street == Street::Turn) {
        if (!physical_turn.has_value() || advanced.value().street != Street::River) {
          return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
        }
        return request_leaf(HuPreflopBestResponseLeafKind::RiverContinuation, advanced.value(),
                            next_history);
      }
      if (state.street != Street::Flop || physical_turn.has_value() ||
          advanced.value().street != Street::Turn) {
        return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }

      std::optional<ComboValues> chance_sum;
      std::uint64_t branch_count = 0U;
      for (std::uint8_t card = 0U; card < 36U; ++card) {
        const auto turn = CardId::from_index(card);
        if (!turn || (turn.value().mask() & flop_board_mask) != 0U) {
          continue;
        }
        auto turn_state = advanced.value();
        turn_state.board_mask = flop_board_mask | turn.value().mask();
        if (!increment(result.physical_turn_branches_visited) || !increment(branch_count)) {
          return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::CountOverflow);
        }
        auto child = visit(turn_state, turn.value(), next_history, depth + 1U);
        if (!child) {
          return child;
        }
        if (!chance_sum.has_value()) {
          chance_sum = std::move(child.value());
        } else {
          std::array<ComboValues, 2> pair{std::move(chance_sum.value()), std::move(child.value())};
          auto reduced = reduce_hu_preflop_best_response_action_values(pair, flop, false);
          if (!reduced) {
            return reduced;
          }
          chance_sum = std::move(reduced.value());
        }
      }
      return branch_count == 33U && chance_sum.has_value()
                 ? Result<ComboValues, HuPreflopError>::success(std::move(chance_sum.value()))
                 : Result<ComboValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }

    if (state.status != HandStatus::InProgress || state.player_to_act > 1U) {
      return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto actions = legal_actions(state, action_config);
    if (!actions || actions.value().empty()) {
      return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    if (!increment(result.decision_nodes_visited) ||
        !increment(state.player_to_act == responding_player
                       ? result.responder_decision_nodes_visited
                       : result.opponent_decision_nodes_visited)) {
      return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::CountOverflow);
    }

    std::vector<ComboValues> action_children;
    action_children.reserve(actions.value().size());
    for (const auto &action : actions.value()) {
      const auto next = apply_action(state, action, action_config);
      if (!next) {
        return Result<ComboValues, HuPreflopError>::failure(HuPreflopError::GameFailure);
      }
      const auto token = std::to_string(static_cast<unsigned>(action.type)) + ":" +
                         std::to_string(action.amount.units()) + ":" +
                         std::to_string(action.requested_basis_points) + ":" +
                         std::to_string(static_cast<unsigned>(action.all_in_kind));
      action_history.push_back(action);
      auto child = visit(next.value(), physical_turn, fnv1a(token, history), depth + 1U);
      action_history.pop_back();
      if (!child) {
        return child;
      }
      action_children.push_back(std::move(child.value()));
    }
    return reduce_hu_preflop_best_response_action_values(action_children, flop,
                                                         state.player_to_act == responding_player);
  };

  auto state = tree.nodes[entry.entry_node].state;
  state.board_mask = flop_board_mask;
  const auto advanced = advance_street(state);
  if (!advanced || advanced.value().street != Street::Flop ||
      advanced.value().status != HandStatus::InProgress) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
        HuPreflopError::GameFailure);
  }
  const auto entry_history = fnv1a("entry:" + std::to_string(entry.entry_node));
  const auto flop_history = fnv1a(
      "street:" + std::to_string(static_cast<unsigned>(advanced.value().street)), entry_history);
  auto values = visit(advanced.value(), std::nullopt, flop_history, 0U);
  if (!values) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(values.error());
  }
  result.values = std::move(values.value());
  result.fingerprint = best_response_task_evaluation_fingerprint(result);
  const auto valid = validate_hu_preflop_best_response_task_evaluation(tree, decomposition, result);
  return valid
             ? Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::success(
                   std::move(result))
             : Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(valid.error());
}

Result<bool, HuPreflopError> validate_hu_preflop_best_response_task_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseTaskEvaluation &evaluation) {
  if (evaluation.major != HuPreflopBestResponseTaskEvaluation::format_major ||
      evaluation.minor > HuPreflopBestResponseTaskEvaluation::format_minor ||
      evaluation.tree_fingerprint != tree.fingerprint ||
      evaluation.tree_fingerprint != decomposition.tree_fingerprint ||
      evaluation.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      evaluation.continuation_fingerprint.empty() || evaluation.blueprint_iterations == 0U ||
      evaluation.responding_player > 1U ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      decomposition.canonical_flops == 0U ||
      evaluation.task_span_index >= decomposition.canonical_public_flop_roots ||
      evaluation.decision_nodes_visited == 0U ||
      evaluation.decision_nodes_visited != evaluation.responder_decision_nodes_visited +
                                               evaluation.opponent_decision_nodes_visited ||
      evaluation.physical_turn_branches_visited == 0U ||
      evaluation.physical_turn_branches_visited % 33U != 0U ||
      evaluation.upper_terminal_leaves_visited == 0U ||
      evaluation.river_continuation_leaves_visited == 0U ||
      evaluation.maximum_recursion_depth == 0U ||
      !valid_best_response_combo_value_rows(evaluation.values, evaluation.flop) ||
      evaluation.fingerprint.empty() ||
      evaluation.fingerprint != best_response_task_evaluation_fingerprint(evaluation)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto entry_index = evaluation.task_span_index / decomposition.canonical_flops;
  const auto flop_index = evaluation.task_span_index % decomposition.canonical_flops;
  if (entry_index >= decomposition.entries.size() ||
      flop_index >= decomposition.canonical_flop_catalog.size() ||
      evaluation.entry_node != decomposition.entries[entry_index].entry_node ||
      evaluation.flop != decomposition.canonical_flop_catalog[flop_index].cards ||
      evaluation.entry_node >= tree.nodes.size() ||
      tree.nodes[evaluation.entry_node].kind != HuPreflopNodeKind::PostflopEntry) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

std::string fingerprint_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator) {
  return river_best_response_leaf_accumulator_fingerprint(accumulator);
}

Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>
make_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopBestResponseLeafQuery &query, std::string continuation_fingerprint,
    const std::uint64_t blueprint_iterations) {
  const auto expected = expected_river_best_response_leaf_roots(catalog, batch_plan, query);
  if (!expected || continuation_fingerprint.empty() || blueprint_iterations == 0U ||
      tree.fingerprint != decomposition.tree_fingerprint ||
      tree.fingerprint != catalog.tree_fingerprint ||
      decomposition.fingerprint != catalog.decomposition_plan_fingerprint ||
      decomposition.blueprint_fingerprint != batch_plan.blueprint_fingerprint) {
    return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
        expected ? HuPreflopError::InvalidConfiguration : expected.error());
  }
  HuPreflopRiverBestResponseLeafAccumulator accumulator;
  accumulator.tree_fingerprint = tree.fingerprint;
  accumulator.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  accumulator.root_catalog_fingerprint = catalog.fingerprint;
  accumulator.batch_plan_fingerprint = batch_plan.fingerprint;
  accumulator.continuation_fingerprint = std::move(continuation_fingerprint);
  accumulator.blueprint_iterations = blueprint_iterations;
  accumulator.task_span_index = query.task_span_index;
  accumulator.entry_node = query.entry_node;
  accumulator.flop = query.flop;
  accumulator.turn = query.turn;
  accumulator.responding_player = query.responding_player;
  accumulator.state = query.state;
  accumulator.action_history = query.action_history;
  accumulator.history_fingerprint = query.history_fingerprint;
  accumulator.expected_resolver_root_ordinals = std::move(expected.value());
  accumulator.contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a("gtosd.hu_preflop_river_best_response_leaf_contributions.v1|" +
                  std::to_string(query.task_span_index) + "|" + std::to_string(query.turn.value()) +
                  "|" + std::to_string(query.responding_player) + "|" + query.history_fingerprint));
  accumulator.fingerprint = river_best_response_leaf_accumulator_fingerprint(accumulator);
  const auto valid = validate_hu_preflop_river_best_response_leaf_accumulator(
      tree, decomposition, catalog, batch_plan, accumulator);
  return valid ? Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::success(
                     std::move(accumulator))
               : Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
                     valid.error());
}

Result<bool, HuPreflopError> validate_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator) {
  HuPreflopBestResponseLeafQuery query;
  query.kind = HuPreflopBestResponseLeafKind::RiverContinuation;
  query.task_span_index = accumulator.task_span_index;
  query.entry_node = accumulator.entry_node;
  query.flop = accumulator.flop;
  query.turn = accumulator.turn;
  query.has_turn = true;
  query.responding_player = accumulator.responding_player;
  query.state = accumulator.state;
  query.action_history = accumulator.action_history;
  query.history_fingerprint = accumulator.history_fingerprint;
  const auto expected = expected_river_best_response_leaf_roots(catalog, batch_plan, query);
  if (!expected || accumulator.major != HuPreflopRiverBestResponseLeafAccumulator::format_major ||
      accumulator.minor > HuPreflopRiverBestResponseLeafAccumulator::format_minor ||
      accumulator.tree_fingerprint != tree.fingerprint ||
      accumulator.tree_fingerprint != decomposition.tree_fingerprint ||
      accumulator.tree_fingerprint != catalog.tree_fingerprint ||
      accumulator.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      accumulator.blueprint_fingerprint != batch_plan.blueprint_fingerprint ||
      accumulator.root_catalog_fingerprint != catalog.fingerprint ||
      accumulator.batch_plan_fingerprint != batch_plan.fingerprint ||
      accumulator.continuation_fingerprint.empty() || accumulator.blueprint_iterations == 0U ||
      decomposition.fingerprint != catalog.decomposition_plan_fingerprint ||
      accumulator.expected_resolver_root_ordinals != expected.value() ||
      accumulator.next_boundary_index > accumulator.expected_resolver_root_ordinals.size() ||
      accumulator.complete !=
          (accumulator.next_boundary_index == accumulator.expected_resolver_root_ordinals.size()) ||
      accumulator.contribution_chain_fingerprint.empty() || accumulator.fingerprint.empty() ||
      accumulator.fingerprint != river_best_response_leaf_accumulator_fingerprint(accumulator)) {
    return Result<bool, HuPreflopError>::failure(expected ? HuPreflopError::IntegrityFailure
                                                          : expected.error());
  }
  const auto masks = combo_masks();
  const auto blocked_mask = flop_mask(accumulator.flop) | accumulator.turn.mask();
  for (std::size_t combo = 0U; combo < accumulator.values.size(); ++combo) {
    const auto &value = accumulator.values[combo];
    if (!std::isfinite(value.weighted_reach_sum) ||
        !std::isfinite(value.weighted_reach_compensation) ||
        !std::isfinite(value.weighted_utility_sum_antes) ||
        !std::isfinite(value.weighted_utility_compensation_antes) ||
        ((masks[combo] & blocked_mask) != 0U &&
         (value.weighted_reach_sum != 0.0 || value.weighted_reach_compensation != 0.0 ||
          value.weighted_utility_sum_antes != 0.0 ||
          value.weighted_utility_compensation_antes != 0.0))) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<bool, HuPreflopError> accumulate_hu_preflop_river_best_response_leaf_boundary(
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    HuPreflopRiverBestResponseLeafAccumulator &accumulator,
    const HuPreflopRiverRootBoundary &boundary) {
  if (accumulator.major != HuPreflopRiverBestResponseLeafAccumulator::format_major ||
      accumulator.minor > HuPreflopRiverBestResponseLeafAccumulator::format_minor ||
      accumulator.complete || accumulator.fingerprint.empty() ||
      accumulator.fingerprint != river_best_response_leaf_accumulator_fingerprint(accumulator) ||
      accumulator.next_boundary_index >= accumulator.expected_resolver_root_ordinals.size() ||
      accumulator.root_catalog_fingerprint != catalog.fingerprint ||
      accumulator.batch_plan_fingerprint != batch_plan.fingerprint ||
      boundary.fingerprint.empty() ||
      boundary.fingerprint != river_root_boundary_fingerprint(boundary) ||
      boundary.value_mode != HuPreflopContinuationValueMode::ExactBestResponse ||
      boundary.tree_fingerprint != accumulator.tree_fingerprint ||
      boundary.blueprint_fingerprint != accumulator.blueprint_fingerprint ||
      boundary.batch_plan_fingerprint != accumulator.batch_plan_fingerprint ||
      boundary.continuation_fingerprint != accumulator.continuation_fingerprint ||
      boundary.blueprint_iterations != accumulator.blueprint_iterations ||
      boundary.resolver_root_ordinal !=
          accumulator.expected_resolver_root_ordinals[accumulator.next_boundary_index] ||
      boundary.task_span_index != accumulator.task_span_index ||
      boundary.entry_node != accumulator.entry_node || boundary.flop != accumulator.flop ||
      boundary.resolving_player != 1U - accumulator.responding_player ||
      boundary.opponent != accumulator.responding_player ||
      !valid_river_root_boundary_components(boundary)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto expected_root =
      hu_preflop_river_resolver_root_at(catalog, batch_plan, boundary.resolver_root_ordinal);
  if (!expected_root || expected_root.value().fingerprint != boundary.resolver_root_fingerprint ||
      expected_root.value().turn != boundary.turn ||
      expected_root.value().river != boundary.river ||
      expected_root.value().resolving_player != boundary.resolving_player) {
    return Result<bool, HuPreflopError>::failure(expected_root ? HuPreflopError::IntegrityFailure
                                                               : expected_root.error());
  }
  const auto component = std::ranges::find(boundary.best_response_turn_components, accumulator.turn,
                                           &HuPreflopRiverBestResponseTurnComponent::turn);
  if (component == boundary.best_response_turn_components.end()) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto masks = combo_masks();
  for (const auto &value : component->values) {
    const auto reach = value.counterfactual_reach;
    const auto utility = reach * value.blueprint_counterfactual_value_antes;
    if (value.opponent_combo >= accumulator.values.size() || !std::isfinite(reach) || reach < 0.0 ||
        !std::isfinite(utility) ||
        ((masks[value.opponent_combo] & accumulator.turn.mask()) != 0U &&
         (reach != 0.0 || utility != 0.0))) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    auto &destination = accumulator.values[value.opponent_combo];
    compensated_add(reach, destination.weighted_reach_sum, destination.weighted_reach_compensation);
    compensated_add(utility, destination.weighted_utility_sum_antes,
                    destination.weighted_utility_compensation_antes);
  }
  ++accumulator.next_boundary_index;
  accumulator.complete =
      accumulator.next_boundary_index == accumulator.expected_resolver_root_ordinals.size();
  accumulator.contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a(boundary.fingerprint, fnv1a(accumulator.contribution_chain_fingerprint)));
  accumulator.fingerprint = river_best_response_leaf_accumulator_fingerprint(accumulator);
  return Result<bool, HuPreflopError>::success(true);
}

Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
finalize_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator) {
  const auto valid = validate_hu_preflop_river_best_response_leaf_accumulator(
      tree, decomposition, catalog, batch_plan, accumulator);
  if (!valid || !accumulator.complete) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        valid ? HuPreflopError::IntegrityFailure : valid.error());
  }
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(accumulator.flop);
  std::vector<HuPreflopBestResponseComboValue> result;
  result.reserve(live_combos_per_flop);
  for (std::size_t combo = 0U; combo < accumulator.values.size(); ++combo) {
    if ((masks[combo] & board_mask) != 0U) {
      continue;
    }
    const auto &source = accumulator.values[combo];
    const auto reach = source.weighted_reach_sum + source.weighted_reach_compensation;
    const auto utility =
        source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
    if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
        (reach == 0.0 && utility != 0.0)) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    result.push_back({static_cast<ComboId>(combo), reach, utility,
                      reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
  }
  HuPreflopBestResponseLeafQuery query;
  query.kind = HuPreflopBestResponseLeafKind::RiverContinuation;
  query.task_span_index = accumulator.task_span_index;
  query.entry_node = accumulator.entry_node;
  query.flop = accumulator.flop;
  query.turn = accumulator.turn;
  query.has_turn = true;
  query.responding_player = accumulator.responding_player;
  query.state = accumulator.state;
  query.action_history = accumulator.action_history;
  query.history_fingerprint = accumulator.history_fingerprint;
  return valid_best_response_leaf_values(query, result)
             ? Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::success(
                   std::move(result))
             : Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
resolve_hu_preflop_best_response_leaf_streaming(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopBestResponseLeafQuery &query,
    const HuPreflopBestResponseTerminalContributionProvider &terminal_provider,
    const HuPreflopBestResponseRiverBoundaryProvider &river_boundary_provider,
    std::string continuation_fingerprint, const std::uint64_t blueprint_iterations,
    const HuPreflopRiverBestResponseLeafAccumulator *resume_from,
    const HuPreflopRiverBestResponseLeafCheckpointSink &checkpoint_sink) {
  if (continuation_fingerprint.empty() || blueprint_iterations == 0U ||
      !valid_river_batch_plan_for_catalog(batch_plan, catalog) ||
      tree.fingerprint != decomposition.tree_fingerprint ||
      tree.fingerprint != catalog.tree_fingerprint ||
      decomposition.fingerprint != catalog.decomposition_plan_fingerprint ||
      decomposition.blueprint_fingerprint != batch_plan.blueprint_fingerprint) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  if (query.kind == HuPreflopBestResponseLeafKind::UpperStreetTerminal) {
    if (!terminal_provider || resume_from != nullptr) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
    const auto contribution = terminal_provider(query);
    if (!contribution) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          contribution.error());
    }
    if (contribution.value().blueprint_iterations != blueprint_iterations ||
        contribution.value().continuation_fingerprint != continuation_fingerprint) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    return select_hu_preflop_upper_street_best_response_leaf(tree, decomposition, catalog,
                                                             contribution.value(), query);
  }
  if (query.kind != HuPreflopBestResponseLeafKind::RiverContinuation || !river_boundary_provider) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  using LeafAccumulator = HuPreflopRiverBestResponseLeafAccumulator;
  using LeafAccumulatorResult = Result<LeafAccumulator, HuPreflopError>;
  std::unique_ptr<LeafAccumulatorResult> initial;
  try {
    initial = std::make_unique<LeafAccumulatorResult>(
        make_hu_preflop_river_best_response_leaf_accumulator(
            tree, decomposition, catalog, batch_plan, query, continuation_fingerprint,
            blueprint_iterations));
  } catch (const std::bad_alloc &) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
  if (!*initial) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        initial->error());
  }
  std::unique_ptr<LeafAccumulator> accumulator;
  try {
    accumulator = std::make_unique<LeafAccumulator>(std::move(initial->value()));
  } catch (const std::bad_alloc &) {
    return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
  if (resume_from != nullptr) {
    const auto valid = validate_hu_preflop_river_best_response_leaf_accumulator(
        tree, decomposition, catalog, batch_plan, *resume_from);
    if (!valid || resume_from->tree_fingerprint != accumulator->tree_fingerprint ||
        resume_from->blueprint_fingerprint != accumulator->blueprint_fingerprint ||
        resume_from->root_catalog_fingerprint != accumulator->root_catalog_fingerprint ||
        resume_from->batch_plan_fingerprint != accumulator->batch_plan_fingerprint ||
        resume_from->continuation_fingerprint != continuation_fingerprint ||
        resume_from->blueprint_iterations != blueprint_iterations ||
        resume_from->task_span_index != query.task_span_index ||
        resume_from->entry_node != query.entry_node || resume_from->flop != query.flop ||
        resume_from->turn != query.turn ||
        resume_from->responding_player != query.responding_player ||
        resume_from->state != query.state || resume_from->action_history != query.action_history ||
        resume_from->history_fingerprint != query.history_fingerprint ||
        resume_from->expected_resolver_root_ordinals !=
            accumulator->expected_resolver_root_ordinals) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          valid ? HuPreflopError::IntegrityFailure : valid.error());
    }
    try {
      accumulator = std::make_unique<LeafAccumulator>(*resume_from);
    } catch (const std::bad_alloc &) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          HuPreflopError::MemoryFailure);
    }
  }
  while (accumulator->next_boundary_index < accumulator->expected_resolver_root_ordinals.size()) {
    const auto ordinal =
        accumulator->expected_resolver_root_ordinals[accumulator->next_boundary_index];
    auto boundary = river_boundary_provider(query, ordinal);
    if (!boundary) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          boundary.error());
    }
    std::unique_ptr<LeafAccumulator> candidate;
    try {
      candidate = std::make_unique<LeafAccumulator>(*accumulator);
    } catch (const std::bad_alloc &) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          HuPreflopError::MemoryFailure);
    }
    const auto accumulated = accumulate_hu_preflop_river_best_response_leaf_boundary(
        catalog, batch_plan, *candidate, boundary.value());
    if (!accumulated) {
      return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
          accumulated.error());
    }
    if (checkpoint_sink) {
      const auto checkpointed = checkpoint_sink(*candidate);
      if (!checkpointed) {
        return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
            checkpointed.error());
      }
      if (!checkpointed.value()) {
        return Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>::failure(
            HuPreflopError::IoFailure);
      }
    }
    accumulator = std::move(candidate);
  }
  return finalize_hu_preflop_river_best_response_leaf_accumulator(tree, decomposition, catalog,
                                                                  batch_plan, *accumulator);
}

Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_task_streaming(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const std::uint64_t task_span_index,
    const HuPreflopBestResponseTerminalContributionProvider &terminal_provider,
    const HuPreflopBestResponseRiverBoundaryProvider &river_boundary_provider,
    std::string continuation_fingerprint, const std::uint8_t responding_player,
    const std::uint64_t blueprint_iterations) {
  if (!terminal_provider || !river_boundary_provider || continuation_fingerprint.empty() ||
      blueprint_iterations == 0U || !valid_river_batch_plan_for_catalog(batch_plan, catalog) ||
      tree.fingerprint != decomposition.tree_fingerprint ||
      tree.fingerprint != catalog.tree_fingerprint ||
      decomposition.fingerprint != catalog.decomposition_plan_fingerprint ||
      decomposition.blueprint_fingerprint != batch_plan.blueprint_fingerprint) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto continuation_identity = continuation_fingerprint;
  const HuPreflopBestResponseLeafProvider leaf_provider =
      [&](const HuPreflopBestResponseLeafQuery &query) {
        return resolve_hu_preflop_best_response_leaf_streaming(
            tree, decomposition, catalog, batch_plan, query, terminal_provider,
            river_boundary_provider, continuation_identity, blueprint_iterations);
      };
  return evaluate_hu_preflop_best_response_task(tree, decomposition, task_span_index, leaf_provider,
                                                std::move(continuation_fingerprint),
                                                responding_player, blueprint_iterations);
}

std::string fingerprint_hu_preflop_best_response_entry_accumulator(
    const HuPreflopBestResponseEntryAccumulator &accumulator) {
  return best_response_entry_accumulator_fingerprint(accumulator);
}

std::string fingerprint_hu_preflop_best_response_entry_evaluation(
    const HuPreflopBestResponseEntryEvaluation &evaluation) {
  return best_response_entry_evaluation_fingerprint(evaluation);
}

Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>
make_hu_preflop_best_response_entry_accumulator(const HuPreflopTree &tree,
                                                const HuPreflopDecompositionPlan &decomposition,
                                                const std::uint64_t entry_index,
                                                std::string continuation_fingerprint,
                                                const std::uint8_t responding_player,
                                                const std::uint64_t blueprint_iterations) {
  std::uint64_t first_task_span_index = 0U;
  if (entry_index >= decomposition.entries.size() || responding_player > 1U ||
      continuation_fingerprint.empty() || blueprint_iterations == 0U ||
      tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      decomposition.canonical_flops == 0U ||
      decomposition.canonical_flops != decomposition.canonical_flop_catalog.size() ||
      !checked_multiply(entry_index, decomposition.canonical_flops, first_task_span_index) ||
      first_task_span_index >= decomposition.canonical_public_flop_roots) {
    return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &entry = decomposition.entries[entry_index];
  if (entry.entry_node >= tree.nodes.size() ||
      tree.nodes[entry.entry_node].kind != HuPreflopNodeKind::PostflopEntry) {
    return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  HuPreflopBestResponseEntryAccumulator accumulator;
  accumulator.tree_fingerprint = tree.fingerprint;
  accumulator.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  accumulator.continuation_fingerprint = std::move(continuation_fingerprint);
  accumulator.blueprint_iterations = blueprint_iterations;
  accumulator.entry_index = entry_index;
  accumulator.entry_node = entry.entry_node;
  accumulator.responding_player = responding_player;
  accumulator.first_task_span_index = first_task_span_index;
  accumulator.task_count = decomposition.canonical_flops;
  accumulator.contribution_chain_fingerprint =
      best_response_entry_initial_chain(entry_index, responding_player);
  accumulator.fingerprint = best_response_entry_accumulator_fingerprint(accumulator);
  const auto valid =
      validate_hu_preflop_best_response_entry_accumulator(tree, decomposition, accumulator);
  return valid ? Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::success(
                     std::move(accumulator))
               : Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
                     valid.error());
}

Result<bool, HuPreflopError> validate_hu_preflop_best_response_entry_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseEntryAccumulator &accumulator) {
  std::uint64_t first_task_span_index = 0U;
  if (accumulator.major != HuPreflopBestResponseEntryAccumulator::format_major ||
      accumulator.minor > HuPreflopBestResponseEntryAccumulator::format_minor ||
      accumulator.tree_fingerprint != tree.fingerprint ||
      accumulator.tree_fingerprint != decomposition.tree_fingerprint ||
      accumulator.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      accumulator.continuation_fingerprint.empty() || accumulator.blueprint_iterations == 0U ||
      accumulator.responding_player > 1U ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      accumulator.entry_index >= decomposition.entries.size() ||
      decomposition.canonical_flops == 0U ||
      !checked_multiply(accumulator.entry_index, decomposition.canonical_flops,
                        first_task_span_index) ||
      accumulator.first_task_span_index != first_task_span_index ||
      accumulator.task_count != decomposition.canonical_flops ||
      accumulator.next_task_offset > accumulator.task_count ||
      accumulator.complete != (accumulator.next_task_offset == accumulator.task_count) ||
      accumulator.entry_node != decomposition.entries[accumulator.entry_index].entry_node ||
      accumulator.entry_node >= tree.nodes.size() ||
      tree.nodes[accumulator.entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      accumulator.contribution_chain_fingerprint.empty() ||
      (accumulator.next_task_offset == 0U &&
       accumulator.contribution_chain_fingerprint !=
           best_response_entry_initial_chain(accumulator.entry_index,
                                             accumulator.responding_player)) ||
      accumulator.fingerprint.empty() ||
      accumulator.fingerprint != best_response_entry_accumulator_fingerprint(accumulator)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  for (const auto &value : accumulator.values) {
    const auto reach = value.weighted_reach_sum + value.weighted_reach_compensation;
    const auto utility =
        value.weighted_utility_sum_antes + value.weighted_utility_compensation_antes;
    if (!std::isfinite(value.weighted_reach_sum) ||
        !std::isfinite(value.weighted_reach_compensation) ||
        !std::isfinite(value.weighted_utility_sum_antes) ||
        !std::isfinite(value.weighted_utility_compensation_antes) || !std::isfinite(reach) ||
        reach < 0.0 || !std::isfinite(utility) || (reach == 0.0 && utility != 0.0)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
    if (accumulator.next_task_offset == 0U &&
        (value.weighted_reach_sum != 0.0 || value.weighted_reach_compensation != 0.0 ||
         value.weighted_utility_sum_antes != 0.0 ||
         value.weighted_utility_compensation_antes != 0.0)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<bool, HuPreflopError> accumulate_hu_preflop_best_response_task_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    HuPreflopBestResponseEntryAccumulator &accumulator,
    const HuPreflopBestResponseTaskEvaluation &evaluation) {
  const auto valid_accumulator =
      validate_hu_preflop_best_response_entry_accumulator(tree, decomposition, accumulator);
  const auto valid_evaluation =
      validate_hu_preflop_best_response_task_evaluation(tree, decomposition, evaluation);
  if (!valid_accumulator || !valid_evaluation || accumulator.complete ||
      evaluation.tree_fingerprint != accumulator.tree_fingerprint ||
      evaluation.blueprint_fingerprint != accumulator.blueprint_fingerprint ||
      evaluation.continuation_fingerprint != accumulator.continuation_fingerprint ||
      evaluation.blueprint_iterations != accumulator.blueprint_iterations ||
      evaluation.entry_node != accumulator.entry_node ||
      evaluation.responding_player != accumulator.responding_player ||
      evaluation.task_span_index !=
          accumulator.first_task_span_index + accumulator.next_task_offset) {
    return Result<bool, HuPreflopError>::failure(
        !valid_accumulator
            ? valid_accumulator.error()
            : (!valid_evaluation ? valid_evaluation.error() : HuPreflopError::IntegrityFailure));
  }

  const auto combos = all_combos();
  for (const auto &value : evaluation.values) {
    if (value.responding_combo >= combos.size()) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto class_id = hand_class(combos[value.responding_combo]);
    if (class_id >= accumulator.values.size()) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    auto &destination = accumulator.values[class_id];
    compensated_add(value.weighted_counterfactual_reach, destination.weighted_reach_sum,
                    destination.weighted_reach_compensation);
    compensated_add(value.weighted_counterfactual_utility_antes,
                    destination.weighted_utility_sum_antes,
                    destination.weighted_utility_compensation_antes);
  }
  ++accumulator.next_task_offset;
  accumulator.complete = accumulator.next_task_offset == accumulator.task_count;
  accumulator.contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a(evaluation.fingerprint, fnv1a(accumulator.contribution_chain_fingerprint)));
  accumulator.fingerprint = best_response_entry_accumulator_fingerprint(accumulator);
  return Result<bool, HuPreflopError>::success(true);
}

Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>
finalize_hu_preflop_best_response_entry_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseEntryAccumulator &accumulator) {
  const auto valid =
      validate_hu_preflop_best_response_entry_accumulator(tree, decomposition, accumulator);
  if (!valid || !accumulator.complete) {
    return Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
        valid ? HuPreflopError::IntegrityFailure : valid.error());
  }
  HuPreflopBestResponseEntryEvaluation result;
  result.tree_fingerprint = accumulator.tree_fingerprint;
  result.blueprint_fingerprint = accumulator.blueprint_fingerprint;
  result.continuation_fingerprint = accumulator.continuation_fingerprint;
  result.accumulator_fingerprint = accumulator.fingerprint;
  result.blueprint_iterations = accumulator.blueprint_iterations;
  result.entry_index = accumulator.entry_index;
  result.entry_node = accumulator.entry_node;
  result.responding_player = accumulator.responding_player;
  result.task_count = accumulator.task_count;
  result.values.reserve(hu_preflop_hand_class_count);
  for (std::size_t class_id = 0U; class_id < accumulator.values.size(); ++class_id) {
    const auto &source = accumulator.values[class_id];
    const auto reach = source.weighted_reach_sum + source.weighted_reach_compensation;
    const auto utility =
        source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
    result.values.push_back({static_cast<HandClassId>(class_id), reach, utility,
                             reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
  }
  result.fingerprint = best_response_entry_evaluation_fingerprint(result);
  const auto valid_result =
      validate_hu_preflop_best_response_entry_evaluation(tree, decomposition, result);
  return valid_result ? Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::success(
                            std::move(result))
                      : Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
                            valid_result.error());
}

Result<bool, HuPreflopError> validate_hu_preflop_best_response_entry_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseEntryEvaluation &evaluation) {
  if (evaluation.major != HuPreflopBestResponseEntryEvaluation::format_major ||
      evaluation.minor > HuPreflopBestResponseEntryEvaluation::format_minor ||
      evaluation.tree_fingerprint != tree.fingerprint ||
      evaluation.tree_fingerprint != decomposition.tree_fingerprint ||
      evaluation.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      evaluation.continuation_fingerprint.empty() || evaluation.accumulator_fingerprint.empty() ||
      evaluation.blueprint_iterations == 0U ||
      evaluation.entry_index >= decomposition.entries.size() ||
      evaluation.entry_node != decomposition.entries[evaluation.entry_index].entry_node ||
      evaluation.responding_player > 1U || evaluation.task_count != decomposition.canonical_flops ||
      evaluation.values.size() != hu_preflop_hand_class_count || evaluation.fingerprint.empty() ||
      evaluation.fingerprint != best_response_entry_evaluation_fingerprint(evaluation)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  std::array<bool, hu_preflop_hand_class_count> observed{};
  for (const auto &value : evaluation.values) {
    if (value.responding_class >= observed.size() || observed[value.responding_class] ||
        !std::isfinite(value.weighted_counterfactual_reach) ||
        value.weighted_counterfactual_reach < 0.0 ||
        !std::isfinite(value.weighted_counterfactual_utility_antes) ||
        !std::isfinite(value.conditional_value_antes) ||
        value.positive_reach != (value.weighted_counterfactual_reach > 0.0)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    if (value.weighted_counterfactual_reach == 0.0) {
      if (value.weighted_counterfactual_utility_antes != 0.0 ||
          value.conditional_value_antes != 0.0) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
    } else {
      const auto expected =
          value.weighted_counterfactual_utility_antes / value.weighted_counterfactual_reach;
      const auto tolerance =
          1.0e-12 * std::max({1.0, std::abs(expected), std::abs(value.conditional_value_antes)});
      if (std::abs(expected - value.conditional_value_antes) > tolerance) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
    }
    observed[value.responding_class] = true;
  }
  return Result<bool, HuPreflopError>::success(true);
}

std::string fingerprint_hu_preflop_best_response_preflop_terminal_evaluation(
    const HuPreflopBestResponsePreflopTerminalEvaluation &evaluation) {
  return best_response_preflop_terminal_evaluation_fingerprint(evaluation);
}

Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_fold_terminal(const HuPreflopTree &tree,
                                                const HuPreflopBlueprint &blueprint,
                                                const std::uint32_t node_id,
                                                std::string continuation_fingerprint,
                                                const std::uint8_t responding_player,
                                                const std::uint64_t blueprint_iterations) {
  const auto valid_blueprint = validate_hu_preflop_blueprint(tree, blueprint);
  if (!valid_blueprint || continuation_fingerprint.empty() || blueprint_iterations == 0U ||
      responding_player > 1U || node_id >= tree.nodes.size() ||
      tree.nodes[node_id].kind != HuPreflopNodeKind::TerminalFold) {
    return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
        valid_blueprint ? HuPreflopError::InvalidConfiguration : valid_blueprint.error());
  }
  const auto reach = preflop_class_reach_at_node(tree, blueprint, node_id);
  const auto payoff = terminal_payoff_antes(tree.nodes[node_id].state, tree.config.rake, 0U);
  if (!reach || !payoff) {
    return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
        !reach ? reach.error() : payoff.error());
  }

  const auto opponent = static_cast<std::uint8_t>(1U - responding_player);
  const auto combos = all_combos();
  HuPreflopComboReach opponent_combo_reach{};
  std::array<double, 36U> opponent_mass_by_card{};
  double opponent_total_mass = 0.0;
  for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
    const auto class_id = hand_class(combos[combo]);
    const auto mass = reach.value().players[opponent][class_id];
    if (!std::isfinite(mass) || mass < 0.0) {
      return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    opponent_combo_reach[combo] = mass;
    opponent_total_mass += mass;
    opponent_mass_by_card[combos[combo].first.value()] += mass;
    opponent_mass_by_card[combos[combo].second.value()] += mass;
  }
  if (!std::isfinite(opponent_total_mass)) {
    return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }

  std::array<HuPreflopCompensatedCounterfactualValue, hu_preflop_hand_class_count> class_values{};
  for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
    auto compatible_mass =
        opponent_total_mass - opponent_mass_by_card[combos[combo].first.value()] -
        opponent_mass_by_card[combos[combo].second.value()] + opponent_combo_reach[combo];
    const auto cancellation_tolerance = 1.0e-12 * std::max(1.0, std::abs(opponent_total_mass));
    if (compatible_mass < 0.0 && compatible_mass >= -cancellation_tolerance) {
      compatible_mass = 0.0;
    }
    const auto weighted_reach =
        compatible_mass * static_cast<double>(ordered_public_runouts_per_private_deal);
    const auto weighted_utility = weighted_reach * payoff.value()[responding_player];
    if (!std::isfinite(weighted_reach) || weighted_reach < 0.0 ||
        !std::isfinite(weighted_utility)) {
      return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    auto &destination = class_values[hand_class(combos[combo])];
    compensated_add(weighted_reach, destination.weighted_reach_sum,
                    destination.weighted_reach_compensation);
    compensated_add(weighted_utility, destination.weighted_utility_sum_antes,
                    destination.weighted_utility_compensation_antes);
  }

  HuPreflopBestResponsePreflopTerminalEvaluation result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = blueprint.fingerprint;
  result.continuation_fingerprint = std::move(continuation_fingerprint);
  result.blueprint_iterations = blueprint_iterations;
  result.node_id = node_id;
  result.kind = HuPreflopBestResponsePreflopLeafKind::TerminalFold;
  result.responding_player = responding_player;
  result.ordered_public_runouts_per_private_deal = ordered_public_runouts_per_private_deal;
  result.values.reserve(hu_preflop_hand_class_count);
  for (std::size_t class_id = 0U; class_id < class_values.size(); ++class_id) {
    const auto &source = class_values[class_id];
    const auto weighted_reach = source.weighted_reach_sum + source.weighted_reach_compensation;
    const auto weighted_utility =
        source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
    result.values.push_back({static_cast<HandClassId>(class_id), weighted_reach, weighted_utility,
                             weighted_reach > 0.0 ? weighted_utility / weighted_reach : 0.0,
                             weighted_reach > 0.0});
  }
  result.fingerprint = best_response_preflop_terminal_evaluation_fingerprint(result);
  const auto valid =
      validate_hu_preflop_best_response_preflop_terminal_evaluation(tree, blueprint, result);
  return valid ? Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::success(
                     std::move(result))
               : Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
                     valid.error());
}

Result<bool, HuPreflopError> validate_hu_preflop_best_response_preflop_terminal_evaluation(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopBestResponsePreflopTerminalEvaluation &evaluation) {
  const auto valid_blueprint = validate_hu_preflop_blueprint(tree, blueprint);
  if (!valid_blueprint) {
    return Result<bool, HuPreflopError>::failure(valid_blueprint.error());
  }
  const auto node_kind = evaluation.node_id < tree.nodes.size()
                             ? tree.nodes[evaluation.node_id].kind
                             : HuPreflopNodeKind::Decision;
  const auto expected_kind = node_kind == HuPreflopNodeKind::TerminalFold
                                 ? HuPreflopBestResponsePreflopLeafKind::TerminalFold
                             : node_kind == HuPreflopNodeKind::TerminalAllIn
                                 ? HuPreflopBestResponsePreflopLeafKind::TerminalAllIn
                                 : HuPreflopBestResponsePreflopLeafKind::PostflopEntry;
  if (evaluation.major != HuPreflopBestResponsePreflopTerminalEvaluation::format_major ||
      evaluation.minor > HuPreflopBestResponsePreflopTerminalEvaluation::format_minor ||
      evaluation.tree_fingerprint != tree.fingerprint ||
      evaluation.blueprint_fingerprint != blueprint.fingerprint ||
      evaluation.continuation_fingerprint.empty() || evaluation.blueprint_iterations == 0U ||
      evaluation.node_id >= tree.nodes.size() || evaluation.kind != expected_kind ||
      (node_kind != HuPreflopNodeKind::TerminalFold &&
       node_kind != HuPreflopNodeKind::TerminalAllIn) ||
      evaluation.responding_player > 1U ||
      evaluation.ordered_public_runouts_per_private_deal !=
          ordered_public_runouts_per_private_deal ||
      !valid_best_response_hand_class_rows(evaluation.values) || evaluation.fingerprint.empty() ||
      evaluation.fingerprint != best_response_preflop_terminal_evaluation_fingerprint(evaluation)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<std::vector<HuPreflopBestResponseHandClassValue>, HuPreflopError>
select_hu_preflop_best_response_preflop_terminal_evaluation(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopBestResponsePreflopLeafQuery &query,
    const HuPreflopBestResponsePreflopTerminalEvaluation &evaluation,
    std::string continuation_fingerprint, const std::uint64_t blueprint_iterations) {
  const auto valid =
      validate_hu_preflop_best_response_preflop_terminal_evaluation(tree, blueprint, evaluation);
  if (!valid || continuation_fingerprint.empty() || query.node_id != evaluation.node_id ||
      query.kind != evaluation.kind || query.responding_player != evaluation.responding_player ||
      continuation_fingerprint != evaluation.continuation_fingerprint ||
      blueprint_iterations != evaluation.blueprint_iterations) {
    return Result<std::vector<HuPreflopBestResponseHandClassValue>, HuPreflopError>::failure(
        !valid ? valid.error() : HuPreflopError::IntegrityFailure);
  }
  return Result<std::vector<HuPreflopBestResponseHandClassValue>, HuPreflopError>::success(
      evaluation.values);
}

std::string fingerprint_hu_preflop_all_in_board_catalog(const HuPreflopAllInBoardCatalog &catalog) {
  return all_in_board_catalog_fingerprint(catalog);
}

std::string fingerprint_hu_preflop_all_in_equity_accumulator(
    const HuPreflopAllInEquityAccumulator &accumulator) {
  return all_in_equity_accumulator_fingerprint(accumulator);
}

std::string fingerprint_hu_preflop_all_in_equity_table(const HuPreflopAllInEquityTable &table) {
  return all_in_equity_table_fingerprint(table);
}

Result<HuPreflopAllInBoardCatalog, HuPreflopError> build_hu_preflop_all_in_board_catalog() {
  HuPreflopAllInBoardCatalog result;
  result.evaluator_contract = std::string(all_in_evaluator_contract);
  result.physical_unordered_boards = physical_unordered_five_card_boards;
  result.ordered_histories_per_unordered_board = ordered_histories_per_unordered_board;
  result.boards = canonical_all_in_board_catalog();
  result.fingerprint = all_in_board_catalog_fingerprint(result);
  const auto valid = validate_hu_preflop_all_in_board_catalog(result);
  return valid ? Result<HuPreflopAllInBoardCatalog, HuPreflopError>::success(std::move(result))
               : Result<HuPreflopAllInBoardCatalog, HuPreflopError>::failure(valid.error());
}

Result<bool, HuPreflopError>
validate_hu_preflop_all_in_board_catalog(const HuPreflopAllInBoardCatalog &catalog) {
  if (catalog.major != HuPreflopAllInBoardCatalog::format_major ||
      catalog.minor > HuPreflopAllInBoardCatalog::format_minor ||
      catalog.evaluator_contract != all_in_evaluator_contract ||
      catalog.physical_unordered_boards != physical_unordered_five_card_boards ||
      catalog.ordered_histories_per_unordered_board != ordered_histories_per_unordered_board ||
      catalog.boards.empty() || catalog.fingerprint.empty() ||
      catalog.fingerprint != all_in_board_catalog_fingerprint(catalog)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  std::uint64_t physical_count = 0U;
  std::array<std::uint8_t, 5> previous{};
  bool has_previous = false;
  for (const auto &board : catalog.boards) {
    std::array<std::uint8_t, 5> values{};
    std::uint64_t mask = 0U;
    for (std::size_t index = 0U; index < board.cards.size(); ++index) {
      const auto card = board.cards[index];
      if (card.value() >= 36U || (mask & card.mask()) != 0U) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      mask |= card.mask();
      values[index] = card.value();
    }
    if (!std::ranges::is_sorted(values) || canonical_all_in_board(values) != values ||
        board.physical_board_count == 0U || (has_previous && !(previous < values)) ||
        !checked_add(physical_count, board.physical_board_count, physical_count)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    previous = values;
    has_previous = true;
  }
  return physical_count == physical_unordered_five_card_boards
             ? Result<bool, HuPreflopError>::success(true)
             : Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

Result<HuPreflopAllInEquityAccumulator, HuPreflopError>
make_hu_preflop_all_in_equity_accumulator(const HuPreflopAllInBoardCatalog &catalog) {
  const auto valid = validate_hu_preflop_all_in_board_catalog(catalog);
  if (!valid) {
    return Result<HuPreflopAllInEquityAccumulator, HuPreflopError>::failure(valid.error());
  }
  HuPreflopAllInEquityAccumulator result;
  result.board_catalog_fingerprint = catalog.fingerprint;
  result.evaluator_contract = catalog.evaluator_contract;
  result.matchups.resize(all_in_matchup_count);
  result.contribution_chain_fingerprint =
      "fnv1a64:" + hex64(fnv1a("gtosd.hu_preflop_all_in_equity_contributions.v1"));
  result.fingerprint = all_in_equity_accumulator_fingerprint(result);
  return Result<HuPreflopAllInEquityAccumulator, HuPreflopError>::success(std::move(result));
}

Result<bool, HuPreflopError>
validate_hu_preflop_all_in_equity_accumulator(const HuPreflopAllInBoardCatalog &catalog,
                                              const HuPreflopAllInEquityAccumulator &accumulator) {
  const auto valid_catalog = validate_hu_preflop_all_in_board_catalog(catalog);
  if (!valid_catalog) {
    return Result<bool, HuPreflopError>::failure(valid_catalog.error());
  }
  if (accumulator.major != HuPreflopAllInEquityAccumulator::format_major ||
      accumulator.minor > HuPreflopAllInEquityAccumulator::format_minor ||
      accumulator.board_catalog_fingerprint != catalog.fingerprint ||
      accumulator.evaluator_contract != catalog.evaluator_contract ||
      accumulator.next_board_ordinal > catalog.boards.size() ||
      accumulator.matchups.size() != all_in_matchup_count ||
      accumulator.complete != (accumulator.next_board_ordinal == catalog.boards.size()) ||
      accumulator.contribution_chain_fingerprint.empty() || accumulator.fingerprint.empty() ||
      accumulator.fingerprint != all_in_equity_accumulator_fingerprint(accumulator)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  std::uint64_t expected_physical_boards = 0U;
  for (std::uint64_t ordinal = 0U; ordinal < accumulator.next_board_ordinal; ++ordinal) {
    if (!checked_add(expected_physical_boards, catalog.boards[ordinal].physical_board_count,
                     expected_physical_boards)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::CountOverflow);
    }
  }
  std::uint64_t observed_outcomes = 0U;
  for (const auto &matchup : accumulator.matchups) {
    std::uint64_t row_total = 0U;
    if (!checked_add(matchup.responding_player_wins, matchup.ties, row_total) ||
        !checked_add(row_total, matchup.responding_player_losses, row_total) ||
        !checked_add(observed_outcomes, row_total, observed_outcomes)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::CountOverflow);
    }
  }
  std::uint64_t expected_outcomes = 0U;
  if (!checked_multiply(expected_physical_boards, ordered_matchup_outcomes_per_physical_board,
                        expected_outcomes) ||
      accumulator.processed_physical_unordered_boards != expected_physical_boards ||
      accumulator.matchup_outcome_count != expected_outcomes ||
      observed_outcomes != expected_outcomes) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  for (std::size_t first = 0U; first < hu_preflop_hand_class_count; ++first) {
    for (std::size_t second = 0U; second < hu_preflop_hand_class_count; ++second) {
      const auto &forward = accumulator.matchups[first * hu_preflop_hand_class_count + second];
      const auto &reverse = accumulator.matchups[second * hu_preflop_hand_class_count + first];
      if (forward.responding_player_wins != reverse.responding_player_losses ||
          forward.ties != reverse.ties ||
          forward.responding_player_losses != reverse.responding_player_wins) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

namespace {

Result<bool, HuPreflopError>
accumulate_hu_preflop_all_in_equity_board_unchecked(const HuPreflopAllInBoardCatalog &catalog,
                                                    HuPreflopAllInEquityAccumulator &accumulator) {
  if (accumulator.complete || accumulator.board_catalog_fingerprint != catalog.fingerprint ||
      accumulator.evaluator_contract != catalog.evaluator_contract ||
      accumulator.next_board_ordinal >= catalog.boards.size() ||
      accumulator.matchups.size() != all_in_matchup_count) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto &board = catalog.boards[accumulator.next_board_ordinal];
  std::uint64_t board_mask = 0U;
  for (const auto card : board.cards) {
    board_mask |= card.mask();
  }
  const auto combos = all_combos();
  const auto masks = combo_masks();
  std::array<HandValue, 630U> hand_values{};
  std::vector<std::size_t> live_combos;
  live_combos.reserve(live_private_combos_per_complete_board);
  for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
    if ((masks[combo] & board_mask) != 0U) {
      continue;
    }
    const std::array<CardId, 7> cards{combos[combo].first, combos[combo].second, board.cards[0],
                                      board.cards[1],      board.cards[2],       board.cards[3],
                                      board.cards[4]};
    const auto evaluated = evaluate_seven(cards);
    if (!evaluated) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::EquityFailure);
    }
    hand_values[combo] = evaluated.value();
    live_combos.push_back(combo);
  }
  if (live_combos.size() != live_private_combos_per_complete_board) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }

  std::vector<HuPreflopAllInMatchupCount> contribution(all_in_matchup_count);
  std::uint64_t outcome_weight = 0U;
  if (!checked_multiply(board.physical_board_count, ordered_histories_per_unordered_board,
                        outcome_weight)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::CountOverflow);
  }
  std::uint64_t contribution_outcomes = 0U;
  for (const auto first : live_combos) {
    const auto first_class = hand_class(combos[first]);
    for (const auto second : live_combos) {
      if ((masks[first] & masks[second]) != 0U) {
        continue;
      }
      const auto second_class = hand_class(combos[second]);
      auto &matchup =
          contribution[static_cast<std::size_t>(first_class) * hu_preflop_hand_class_count +
                       static_cast<std::size_t>(second_class)];
      auto *destination = hand_values[first] > hand_values[second] ? &matchup.responding_player_wins
                          : hand_values[second] > hand_values[first]
                              ? &matchup.responding_player_losses
                              : &matchup.ties;
      if (!checked_add(*destination, outcome_weight, *destination) ||
          !checked_add(contribution_outcomes, outcome_weight, contribution_outcomes)) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::CountOverflow);
      }
    }
  }
  std::uint64_t expected_contribution_outcomes = 0U;
  if (!checked_multiply(board.physical_board_count, ordered_matchup_outcomes_per_physical_board,
                        expected_contribution_outcomes) ||
      contribution_outcomes != expected_contribution_outcomes) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }

  auto candidate = accumulator;
  for (std::size_t index = 0U; index < candidate.matchups.size(); ++index) {
    const auto &source = contribution[index];
    auto &destination = candidate.matchups[index];
    if (!checked_add(destination.responding_player_wins, source.responding_player_wins,
                     destination.responding_player_wins) ||
        !checked_add(destination.ties, source.ties, destination.ties) ||
        !checked_add(destination.responding_player_losses, source.responding_player_losses,
                     destination.responding_player_losses)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::CountOverflow);
    }
  }
  ++candidate.next_board_ordinal;
  if (!checked_add(candidate.processed_physical_unordered_boards, board.physical_board_count,
                   candidate.processed_physical_unordered_boards) ||
      !checked_add(candidate.matchup_outcome_count, contribution_outcomes,
                   candidate.matchup_outcome_count)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::CountOverflow);
  }
  candidate.complete = candidate.next_board_ordinal == catalog.boards.size();
  const auto board_identity = std::to_string(candidate.next_board_ordinal - 1U) + "|" +
                              std::to_string(board.physical_board_count) + "|" +
                              std::to_string(contribution_outcomes);
  candidate.contribution_chain_fingerprint =
      "fnv1a64:" + hex64(fnv1a(board_identity, fnv1a(candidate.contribution_chain_fingerprint)));
  candidate.fingerprint = all_in_equity_accumulator_fingerprint(candidate);
  accumulator = std::move(candidate);
  return Result<bool, HuPreflopError>::success(true);
}

} // namespace

Result<bool, HuPreflopError>
accumulate_hu_preflop_all_in_equity_board(const HuPreflopAllInBoardCatalog &catalog,
                                          HuPreflopAllInEquityAccumulator &accumulator) {
  const auto valid = validate_hu_preflop_all_in_equity_accumulator(catalog, accumulator);
  if (!valid) {
    return Result<bool, HuPreflopError>::failure(valid.error());
  }
  const auto accumulated =
      accumulate_hu_preflop_all_in_equity_board_unchecked(catalog, accumulator);
  if (!accumulated) {
    return accumulated;
  }
  return validate_hu_preflop_all_in_equity_accumulator(catalog, accumulator);
}

Result<HuPreflopAllInEquityTable, HuPreflopError>
finalize_hu_preflop_all_in_equity_accumulator(const HuPreflopAllInBoardCatalog &catalog,
                                              const HuPreflopAllInEquityAccumulator &accumulator) {
  const auto valid = validate_hu_preflop_all_in_equity_accumulator(catalog, accumulator);
  if (!valid || !accumulator.complete) {
    return Result<HuPreflopAllInEquityTable, HuPreflopError>::failure(
        !valid ? valid.error() : HuPreflopError::IntegrityFailure);
  }
  HuPreflopAllInEquityTable result;
  result.board_catalog_fingerprint = catalog.fingerprint;
  result.evaluator_contract = catalog.evaluator_contract;
  result.canonical_board_count = catalog.boards.size();
  result.physical_unordered_boards = catalog.physical_unordered_boards;
  result.ordered_public_runouts_per_private_deal = ordered_public_runouts_per_private_deal;
  result.matchup_outcome_count = accumulator.matchup_outcome_count;
  result.matchups = accumulator.matchups;
  result.accumulator_fingerprint = accumulator.fingerprint;
  result.fingerprint = all_in_equity_table_fingerprint(result);
  const auto valid_table = validate_hu_preflop_all_in_equity_table(catalog, result);
  return valid_table
             ? Result<HuPreflopAllInEquityTable, HuPreflopError>::success(std::move(result))
             : Result<HuPreflopAllInEquityTable, HuPreflopError>::failure(valid_table.error());
}

Result<HuPreflopAllInEquityTable, HuPreflopError>
build_hu_preflop_all_in_equity_table(const HuPreflopAllInBoardCatalog &catalog) {
  const auto valid_catalog = validate_hu_preflop_all_in_board_catalog(catalog);
  if (!valid_catalog) {
    return Result<HuPreflopAllInEquityTable, HuPreflopError>::failure(valid_catalog.error());
  }
  auto accumulator = make_hu_preflop_all_in_equity_accumulator(catalog);
  if (!accumulator) {
    return Result<HuPreflopAllInEquityTable, HuPreflopError>::failure(accumulator.error());
  }
  while (!accumulator.value().complete) {
    const auto accumulated =
        accumulate_hu_preflop_all_in_equity_board_unchecked(catalog, accumulator.value());
    if (!accumulated) {
      return Result<HuPreflopAllInEquityTable, HuPreflopError>::failure(accumulated.error());
    }
  }
  return finalize_hu_preflop_all_in_equity_accumulator(catalog, accumulator.value());
}

Result<HuPreflopAllInTrainingOracle, HuPreflopError>
make_hu_preflop_all_in_training_oracle(const HuPreflopAllInEquityTable &table) {
  if (table.matchups.size() !=
          hu_preflop_hand_class_count * hu_preflop_hand_class_count ||
      table.fingerprint.empty() || table.fingerprint != all_in_equity_table_fingerprint(table)) {
    return Result<HuPreflopAllInTrainingOracle, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  HuPreflopAllInTrainingOracle result;
  result.source_equity_table_fingerprint = table.fingerprint;
  for (std::size_t index = 0U; index < table.matchups.size(); ++index) {
    const auto &source = table.matchups[index];
    std::uint64_t total = 0U;
    if (!checked_add(source.responding_player_wins, source.ties, total) ||
        !checked_add(total, source.responding_player_losses, total) || total == 0U) {
      return Result<HuPreflopAllInTrainingOracle, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.matchups[index].win_probability =
        static_cast<double>(source.responding_player_wins) / static_cast<double>(total);
    result.matchups[index].tie_probability =
        static_cast<double>(source.ties) / static_cast<double>(total);
  }
  const auto valid = validate_hu_preflop_all_in_training_oracle(result);
  return valid ? Result<HuPreflopAllInTrainingOracle, HuPreflopError>::success(std::move(result))
               : Result<HuPreflopAllInTrainingOracle, HuPreflopError>::failure(valid.error());
}

Result<bool, HuPreflopError>
validate_hu_preflop_all_in_equity_table(const HuPreflopAllInBoardCatalog &catalog,
                                        const HuPreflopAllInEquityTable &table) {
  const auto valid_catalog = validate_hu_preflop_all_in_board_catalog(catalog);
  if (!valid_catalog) {
    return Result<bool, HuPreflopError>::failure(valid_catalog.error());
  }
  std::uint64_t expected_outcomes = 0U;
  if (!checked_multiply(physical_private_deals, ordered_public_runouts_per_private_deal,
                        expected_outcomes) ||
      table.major != HuPreflopAllInEquityTable::format_major ||
      table.minor > HuPreflopAllInEquityTable::format_minor ||
      table.board_catalog_fingerprint != catalog.fingerprint ||
      table.evaluator_contract != catalog.evaluator_contract ||
      table.canonical_board_count != catalog.boards.size() ||
      table.physical_unordered_boards != catalog.physical_unordered_boards ||
      table.ordered_public_runouts_per_private_deal != ordered_public_runouts_per_private_deal ||
      table.matchup_outcome_count != expected_outcomes ||
      table.matchups.size() != all_in_matchup_count || table.accumulator_fingerprint.empty() ||
      table.fingerprint.empty() || table.fingerprint != all_in_equity_table_fingerprint(table)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto combos = all_combos();
  const auto masks = combo_masks();
  std::array<std::uint64_t, all_in_matchup_count> compatible_pairs{};
  for (std::size_t first = 0U; first < combos.size(); ++first) {
    const auto first_class = hand_class(combos[first]);
    for (std::size_t second = 0U; second < combos.size(); ++second) {
      if ((masks[first] & masks[second]) == 0U) {
        const auto second_class = hand_class(combos[second]);
        ++compatible_pairs[static_cast<std::size_t>(first_class) * hu_preflop_hand_class_count +
                           static_cast<std::size_t>(second_class)];
      }
    }
  }
  std::uint64_t observed_outcomes = 0U;
  for (std::size_t first = 0U; first < hu_preflop_hand_class_count; ++first) {
    for (std::size_t second = 0U; second < hu_preflop_hand_class_count; ++second) {
      const auto index = first * hu_preflop_hand_class_count + second;
      const auto &forward = table.matchups[index];
      const auto &reverse = table.matchups[second * hu_preflop_hand_class_count + first];
      std::uint64_t cell_total = 0U;
      std::uint64_t expected_cell_total = 0U;
      if (!checked_add(forward.responding_player_wins, forward.ties, cell_total) ||
          !checked_add(cell_total, forward.responding_player_losses, cell_total) ||
          !checked_multiply(compatible_pairs[index], ordered_public_runouts_per_private_deal,
                            expected_cell_total) ||
          cell_total != expected_cell_total ||
          forward.responding_player_wins != reverse.responding_player_losses ||
          forward.ties != reverse.ties ||
          forward.responding_player_losses != reverse.responding_player_wins ||
          !checked_add(observed_outcomes, cell_total, observed_outcomes)) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
    }
  }
  return observed_outcomes == expected_outcomes
             ? Result<bool, HuPreflopError>::success(true)
             : Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

namespace {

Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_all_in_terminal_validated(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopAllInEquityTable &equity_table, const std::uint32_t node_id,
    std::string continuation_fingerprint, const std::uint8_t responding_player,
    const std::uint64_t blueprint_iterations) {
  const auto reach = preflop_class_reach_at_node(tree, blueprint, node_id);
  const auto responder_win_mask = static_cast<std::uint8_t>(1U << responding_player);
  const auto opponent_win_mask = static_cast<std::uint8_t>(1U << (1U - responding_player));
  const auto payoff_win =
      terminal_payoff_antes(tree.nodes[node_id].state, tree.config.rake, responder_win_mask);
  const auto payoff_tie = terminal_payoff_antes(tree.nodes[node_id].state, tree.config.rake, 0b11U);
  const auto payoff_loss =
      terminal_payoff_antes(tree.nodes[node_id].state, tree.config.rake, opponent_win_mask);
  if (!reach || !payoff_win || !payoff_tie || !payoff_loss) {
    return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
        !reach ? reach.error() : HuPreflopError::GameFailure);
  }

  const auto opponent = static_cast<std::uint8_t>(1U - responding_player);
  HuPreflopBestResponsePreflopTerminalEvaluation result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = blueprint.fingerprint;
  result.continuation_fingerprint = std::move(continuation_fingerprint);
  result.blueprint_iterations = blueprint_iterations;
  result.node_id = node_id;
  result.kind = HuPreflopBestResponsePreflopLeafKind::TerminalAllIn;
  result.responding_player = responding_player;
  result.ordered_public_runouts_per_private_deal = ordered_public_runouts_per_private_deal;
  result.values.reserve(hu_preflop_hand_class_count);
  for (std::size_t responding_class = 0U; responding_class < hu_preflop_hand_class_count;
       ++responding_class) {
    HuPreflopCompensatedCounterfactualValue accumulated;
    for (std::size_t opponent_class = 0U; opponent_class < hu_preflop_hand_class_count;
         ++opponent_class) {
      const auto opponent_reach = reach.value().players[opponent][opponent_class];
      if (!std::isfinite(opponent_reach) || opponent_reach < 0.0) {
        return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      const auto &matchup =
          equity_table.matchups[responding_class * hu_preflop_hand_class_count + opponent_class];
      const auto matchup_reach = static_cast<double>(matchup.responding_player_wins) +
                                 static_cast<double>(matchup.ties) +
                                 static_cast<double>(matchup.responding_player_losses);
      const auto matchup_utility =
          static_cast<double>(matchup.responding_player_wins) *
              payoff_win.value()[responding_player] +
          static_cast<double>(matchup.ties) * payoff_tie.value()[responding_player] +
          static_cast<double>(matchup.responding_player_losses) *
              payoff_loss.value()[responding_player];
      compensated_add(opponent_reach * matchup_reach, accumulated.weighted_reach_sum,
                      accumulated.weighted_reach_compensation);
      compensated_add(opponent_reach * matchup_utility, accumulated.weighted_utility_sum_antes,
                      accumulated.weighted_utility_compensation_antes);
    }
    const auto weighted_reach =
        accumulated.weighted_reach_sum + accumulated.weighted_reach_compensation;
    const auto weighted_utility =
        accumulated.weighted_utility_sum_antes + accumulated.weighted_utility_compensation_antes;
    if (!std::isfinite(weighted_reach) || weighted_reach < 0.0 ||
        !std::isfinite(weighted_utility) || (weighted_reach == 0.0 && weighted_utility != 0.0)) {
      return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    result.values.push_back(
        {static_cast<HandClassId>(responding_class), weighted_reach, weighted_utility,
         weighted_reach > 0.0 ? weighted_utility / weighted_reach : 0.0, weighted_reach > 0.0});
  }
  result.fingerprint = best_response_preflop_terminal_evaluation_fingerprint(result);
  const auto valid =
      validate_hu_preflop_best_response_preflop_terminal_evaluation(tree, blueprint, result);
  return valid ? Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::success(
                     std::move(result))
               : Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
                     valid.error());
}

} // namespace

Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_all_in_terminal(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopAllInBoardCatalog &catalog, const HuPreflopAllInEquityTable &equity_table,
    const std::uint32_t node_id, std::string continuation_fingerprint,
    const std::uint8_t responding_player, const std::uint64_t blueprint_iterations) {
  const auto valid_blueprint = validate_hu_preflop_blueprint(tree, blueprint);
  const auto valid_table = validate_hu_preflop_all_in_equity_table(catalog, equity_table);
  if (!valid_blueprint || !valid_table || continuation_fingerprint.empty() ||
      blueprint_iterations == 0U || responding_player > 1U || node_id >= tree.nodes.size() ||
      tree.nodes[node_id].kind != HuPreflopNodeKind::TerminalAllIn) {
    return Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>::failure(
        !valid_blueprint
            ? valid_blueprint.error()
            : (!valid_table ? valid_table.error() : HuPreflopError::InvalidConfiguration));
  }
  return evaluate_hu_preflop_best_response_all_in_terminal_validated(
      tree, blueprint, equity_table, node_id, std::move(continuation_fingerprint),
      responding_player, blueprint_iterations);
}

std::string fingerprint_hu_preflop_best_response_preflop_evaluation(
    const HuPreflopBestResponsePreflopEvaluation &evaluation) {
  return best_response_preflop_evaluation_fingerprint(evaluation);
}

std::string
fingerprint_hu_preflop_exact_profile_evaluation(const HuPreflopExactProfileEvaluation &evaluation) {
  return exact_profile_evaluation_fingerprint(evaluation);
}

Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_preflop(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponsePreflopLeafProvider &leaf_provider,
    std::string continuation_fingerprint, const std::uint8_t responding_player,
    const std::uint64_t blueprint_iterations) {
  if (!leaf_provider || continuation_fingerprint.empty() || responding_player > 1U ||
      blueprint_iterations == 0U || tree.nodes.empty() || tree.root >= tree.nodes.size() ||
      tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition)) {
    return Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  HuPreflopBestResponsePreflopEvaluation result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  result.continuation_fingerprint = std::move(continuation_fingerprint);
  result.blueprint_iterations = blueprint_iterations;
  result.responding_player = responding_player;
  const auto increment = [](std::uint64_t &counter) {
    if (counter == std::numeric_limits<std::uint64_t>::max()) {
      return false;
    }
    ++counter;
    return true;
  };

  using ClassValues = std::vector<HuPreflopBestResponseHandClassValue>;
  std::function<Result<ClassValues, HuPreflopError>(std::uint32_t, std::uint64_t)> visit;
  visit = [&](const std::uint32_t node_id,
              const std::uint64_t depth) -> Result<ClassValues, HuPreflopError> {
    if (node_id >= tree.nodes.size()) {
      return Result<ClassValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    result.maximum_recursion_depth = std::max(result.maximum_recursion_depth, depth);
    const auto &node = tree.nodes[node_id];
    if (node.kind != HuPreflopNodeKind::Decision) {
      HuPreflopBestResponsePreflopLeafQuery query;
      query.node_id = node_id;
      query.responding_player = responding_player;
      std::uint64_t *counter = nullptr;
      switch (node.kind) {
      case HuPreflopNodeKind::PostflopEntry:
        query.kind = HuPreflopBestResponsePreflopLeafKind::PostflopEntry;
        counter = &result.postflop_entry_leaves_visited;
        break;
      case HuPreflopNodeKind::TerminalFold:
        query.kind = HuPreflopBestResponsePreflopLeafKind::TerminalFold;
        counter = &result.terminal_fold_leaves_visited;
        break;
      case HuPreflopNodeKind::TerminalAllIn:
        query.kind = HuPreflopBestResponsePreflopLeafKind::TerminalAllIn;
        counter = &result.terminal_all_in_leaves_visited;
        break;
      case HuPreflopNodeKind::Decision:
        break;
      }
      if (counter == nullptr || !increment(*counter)) {
        return Result<ClassValues, HuPreflopError>::failure(HuPreflopError::CountOverflow);
      }
      auto values = leaf_provider(query);
      return values && valid_best_response_hand_class_rows(values.value())
                 ? Result<ClassValues, HuPreflopError>::success(std::move(values.value()))
                 : Result<ClassValues, HuPreflopError>::failure(
                       values ? HuPreflopError::IntegrityFailure : values.error());
    }

    if (node.state.player_to_act > 1U || node.edges.empty()) {
      return Result<ClassValues, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    if (!increment(result.decision_nodes_visited) ||
        !increment(node.state.player_to_act == responding_player
                       ? result.responder_decision_nodes_visited
                       : result.opponent_decision_nodes_visited)) {
      return Result<ClassValues, HuPreflopError>::failure(HuPreflopError::CountOverflow);
    }
    std::vector<ClassValues> children;
    children.reserve(node.edges.size());
    for (const auto &edge : node.edges) {
      auto child = visit(edge.child, depth + 1U);
      if (!child) {
        return child;
      }
      children.push_back(std::move(child.value()));
    }
    return reduce_best_response_hand_class_action_values(children, node.state.player_to_act ==
                                                                       responding_player);
  };

  auto root_values = visit(tree.root, 0U);
  if (!root_values) {
    return Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::failure(
        root_values.error());
  }
  result.values = std::move(root_values.value());
  result.fingerprint = best_response_preflop_evaluation_fingerprint(result);
  const auto valid =
      validate_hu_preflop_best_response_preflop_evaluation(tree, decomposition, result);
  return valid ? Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::success(
                     std::move(result))
               : Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::failure(
                     valid.error());
}

Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_whole_game(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopDecompositionPlan &decomposition,
    const std::span<const HuPreflopBestResponseEntryEvaluation> postflop_entries,
    const std::span<const HuPreflopBestResponsePreflopTerminalEvaluation> preflop_terminals,
    std::string continuation_fingerprint, const std::uint8_t responding_player,
    const std::uint64_t blueprint_iterations) {
  const auto valid_blueprint = validate_hu_preflop_blueprint(tree, blueprint);
  if (!valid_blueprint || continuation_fingerprint.empty() || responding_player > 1U ||
      blueprint_iterations == 0U || decomposition.tree_fingerprint != tree.fingerprint ||
      decomposition.blueprint_fingerprint != blueprint.fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      postflop_entries.size() != tree.stats.postflop_entries ||
      preflop_terminals.size() != tree.stats.terminal_folds + tree.stats.terminal_all_ins) {
    return Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::failure(
        valid_blueprint ? HuPreflopError::InvalidConfiguration : valid_blueprint.error());
  }

  std::map<std::uint32_t, const HuPreflopBestResponseEntryEvaluation *> entries_by_node;
  for (const auto &entry : postflop_entries) {
    const auto valid_entry =
        validate_hu_preflop_best_response_entry_evaluation(tree, decomposition, entry);
    if (!valid_entry || entry.continuation_fingerprint != continuation_fingerprint ||
        entry.blueprint_iterations != blueprint_iterations ||
        entry.responding_player != responding_player ||
        !entries_by_node.emplace(entry.entry_node, &entry).second) {
      return Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::failure(
          !valid_entry ? valid_entry.error() : HuPreflopError::IntegrityFailure);
    }
  }

  std::map<std::uint32_t, const HuPreflopBestResponsePreflopTerminalEvaluation *> terminals_by_node;
  std::uint64_t fold_count = 0U;
  std::uint64_t all_in_count = 0U;
  for (const auto &terminal : preflop_terminals) {
    const auto valid_terminal =
        validate_hu_preflop_best_response_preflop_terminal_evaluation(tree, blueprint, terminal);
    if (!valid_terminal || terminal.continuation_fingerprint != continuation_fingerprint ||
        terminal.blueprint_iterations != blueprint_iterations ||
        terminal.responding_player != responding_player ||
        !terminals_by_node.emplace(terminal.node_id, &terminal).second) {
      return Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::failure(
          !valid_terminal ? valid_terminal.error() : HuPreflopError::IntegrityFailure);
    }
    fold_count += terminal.kind == HuPreflopBestResponsePreflopLeafKind::TerminalFold ? 1U : 0U;
    all_in_count += terminal.kind == HuPreflopBestResponsePreflopLeafKind::TerminalAllIn ? 1U : 0U;
  }
  if (fold_count != tree.stats.terminal_folds || all_in_count != tree.stats.terminal_all_ins) {
    return Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  const HuPreflopBestResponsePreflopLeafProvider leaf_provider =
      [&, continuation_identity =
              continuation_fingerprint](const HuPreflopBestResponsePreflopLeafQuery &query) {
        using Values = std::vector<HuPreflopBestResponseHandClassValue>;
        if (query.kind == HuPreflopBestResponsePreflopLeafKind::PostflopEntry) {
          const auto found = entries_by_node.find(query.node_id);
          if (found == entries_by_node.end() || query.responding_player != responding_player) {
            return Result<Values, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
          }
          return Result<Values, HuPreflopError>::success(found->second->values);
        }
        const auto found = terminals_by_node.find(query.node_id);
        if (found == terminals_by_node.end()) {
          return Result<Values, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
        }
        auto selected = select_hu_preflop_best_response_preflop_terminal_evaluation(
            tree, blueprint, query, *found->second, continuation_identity, blueprint_iterations);
        return selected;
      };
  return evaluate_hu_preflop_best_response_preflop(tree, decomposition, leaf_provider,
                                                   std::move(continuation_fingerprint),
                                                   responding_player, blueprint_iterations);
}

Result<bool, HuPreflopError> validate_hu_preflop_best_response_preflop_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponsePreflopEvaluation &evaluation) {
  if (evaluation.major != HuPreflopBestResponsePreflopEvaluation::format_major ||
      evaluation.minor > HuPreflopBestResponsePreflopEvaluation::format_minor ||
      evaluation.tree_fingerprint != tree.fingerprint ||
      evaluation.tree_fingerprint != decomposition.tree_fingerprint ||
      evaluation.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      evaluation.continuation_fingerprint.empty() || evaluation.blueprint_iterations == 0U ||
      evaluation.responding_player > 1U ||
      evaluation.decision_nodes_visited != tree.stats.decision_nodes ||
      evaluation.decision_nodes_visited != evaluation.responder_decision_nodes_visited +
                                               evaluation.opponent_decision_nodes_visited ||
      evaluation.postflop_entry_leaves_visited != tree.stats.postflop_entries ||
      evaluation.terminal_fold_leaves_visited != tree.stats.terminal_folds ||
      evaluation.terminal_all_in_leaves_visited != tree.stats.terminal_all_ins ||
      evaluation.maximum_recursion_depth != tree.stats.maximum_depth ||
      !valid_best_response_hand_class_rows(evaluation.values) || evaluation.fingerprint.empty() ||
      evaluation.fingerprint != best_response_preflop_evaluation_fingerprint(evaluation)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

namespace {

struct HuPreflopExactTerminalProfile {
  std::array<double, 2> values_antes{};
  std::uint64_t fold_count{0U};
  std::uint64_t all_in_count{0U};
};

Result<HuPreflopExactTerminalProfile, HuPreflopError>
evaluate_hu_preflop_exact_terminal_profile_validated(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopAllInEquityTable &all_in_equity_table,
    const std::string &continuation_fingerprint, const std::uint64_t blueprint_iterations,
    const std::uint64_t chance_outcome_count) {
  std::array<HuPreflopCompensatedCounterfactualValue, 2> terminal_values{};
  HuPreflopExactTerminalProfile result;
  for (const auto &node : tree.nodes) {
    if (node.kind != HuPreflopNodeKind::TerminalFold &&
        node.kind != HuPreflopNodeKind::TerminalAllIn) {
      continue;
    }
    const auto reach = preflop_class_reach_at_node(tree, blueprint, node.id);
    if (!reach) {
      return Result<HuPreflopExactTerminalProfile, HuPreflopError>::failure(reach.error());
    }
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      const auto terminal = node.kind == HuPreflopNodeKind::TerminalFold
                                ? evaluate_hu_preflop_best_response_fold_terminal(
                                      tree, blueprint, node.id, continuation_fingerprint, player,
                                      blueprint_iterations)
                                : evaluate_hu_preflop_best_response_all_in_terminal_validated(
                                      tree, blueprint, all_in_equity_table, node.id,
                                      continuation_fingerprint, player, blueprint_iterations);
      if (!terminal) {
        return Result<HuPreflopExactTerminalProfile, HuPreflopError>::failure(terminal.error());
      }
      for (const auto &value : terminal.value().values) {
        const auto own_reach = reach.value().players[player][value.responding_class];
        const auto utility = own_reach * value.weighted_counterfactual_utility_antes;
        if (!std::isfinite(own_reach) || own_reach < 0.0 || !std::isfinite(utility)) {
          return Result<HuPreflopExactTerminalProfile, HuPreflopError>::failure(
              HuPreflopError::NumericalFailure);
        }
        compensated_add(utility, terminal_values[player].weighted_utility_sum_antes,
                        terminal_values[player].weighted_utility_compensation_antes);
      }
    }
    result.fold_count += node.kind == HuPreflopNodeKind::TerminalFold ? 1U : 0U;
    result.all_in_count += node.kind == HuPreflopNodeKind::TerminalAllIn ? 1U : 0U;
  }
  for (std::size_t player = 0U; player < 2U; ++player) {
    result.values_antes[player] = (terminal_values[player].weighted_utility_sum_antes +
                                   terminal_values[player].weighted_utility_compensation_antes) /
                                  static_cast<double>(chance_outcome_count);
  }
  return Result<HuPreflopExactTerminalProfile, HuPreflopError>::success(std::move(result));
}

} // namespace

Result<bool, HuPreflopError>
validate_hu_preflop_exact_profile_evaluation(const HuPreflopTree &tree,
                                             const HuPreflopBlueprint &blueprint,
                                             const HuPreflopDecompositionPlan &decomposition,
                                             const HuPreflopWholeGameCoverageAccumulator &coverage,
                                             const HuPreflopAllInBoardCatalog &all_in_catalog,
                                             const HuPreflopAllInEquityTable &all_in_equity_table,
                                             const HuPreflopExactProfileEvaluation &evaluation) {
  const auto valid_blueprint = validate_hu_preflop_blueprint(tree, blueprint);
  const auto valid_coverage =
      validate_hu_preflop_whole_game_coverage_accumulator(tree, decomposition, coverage);
  const auto valid_table =
      validate_hu_preflop_all_in_equity_table(all_in_catalog, all_in_equity_table);
  std::uint64_t expected_chance_outcomes = 0U;
  if (!valid_blueprint || !valid_coverage || !valid_table ||
      !checked_multiply(decomposition.physical_private_deals,
                        ordered_public_runouts_per_private_deal, expected_chance_outcomes) ||
      evaluation.major != HuPreflopExactProfileEvaluation::format_major ||
      evaluation.minor > HuPreflopExactProfileEvaluation::format_minor ||
      evaluation.tree_fingerprint != tree.fingerprint ||
      evaluation.blueprint_fingerprint != blueprint.fingerprint ||
      evaluation.plan_fingerprint != decomposition.fingerprint ||
      evaluation.continuation_profile_fingerprint !=
          fingerprint_hu_preflop_continuation_profile(coverage) ||
      evaluation.all_in_equity_table_fingerprint != all_in_equity_table.fingerprint ||
      evaluation.blueprint_iterations == 0U ||
      evaluation.blueprint_iterations != coverage.blueprint_iterations ||
      evaluation.chance_outcome_count != expected_chance_outcomes ||
      evaluation.postflop_boundary_count !=
          static_cast<std::uint64_t>(coverage.task_side_coverage.size()) * 2U ||
      evaluation.terminal_fold_count != tree.stats.terminal_folds ||
      evaluation.terminal_all_in_count != tree.stats.terminal_all_ins ||
      evaluation.fingerprint.empty() ||
      evaluation.fingerprint != exact_profile_evaluation_fingerprint(evaluation)) {
    return Result<bool, HuPreflopError>::failure(
        !valid_blueprint ? valid_blueprint.error()
                         : (!valid_coverage ? valid_coverage.error()
                                            : (!valid_table ? valid_table.error()
                                                            : HuPreflopError::IntegrityFailure)));
  }

  const auto expected_terminal = evaluate_hu_preflop_exact_terminal_profile_validated(
      tree, blueprint, all_in_equity_table, evaluation.continuation_profile_fingerprint,
      evaluation.blueprint_iterations, expected_chance_outcomes);
  if (!expected_terminal ||
      expected_terminal.value().fold_count != evaluation.terminal_fold_count ||
      expected_terminal.value().all_in_count != evaluation.terminal_all_in_count) {
    return Result<bool, HuPreflopError>::failure(
        !expected_terminal ? expected_terminal.error() : HuPreflopError::IntegrityFailure);
  }

  std::array<double, 2> expected_postflop{};
  for (std::size_t task = 0U; task < coverage.task_side_coverage.size(); ++task) {
    if (coverage.task_side_coverage[task] != 0x3U) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    expected_postflop[1] += coverage.task_side_profile_utility_antes[task * 2U];
    expected_postflop[0] += coverage.task_side_profile_utility_antes[task * 2U + 1U];
  }
  for (std::size_t player = 0U; player < 2U; ++player) {
    const auto postflop = evaluation.postflop_values_antes[player];
    const auto terminal = evaluation.preflop_terminal_values_antes[player];
    const auto total = evaluation.total_values_antes[player];
    const auto tolerance =
        1.0e-11 * std::max({1.0, std::abs(expected_postflop[player]), std::abs(postflop),
                            std::abs(terminal), std::abs(total)});
    if (!std::isfinite(postflop) || !std::isfinite(terminal) || !std::isfinite(total) ||
        std::abs(postflop - expected_postflop[player]) > tolerance ||
        std::abs(terminal - expected_terminal.value().values_antes[player]) > tolerance ||
        std::abs(total - (postflop + terminal)) > tolerance) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<HuPreflopExactProfileEvaluation, HuPreflopError>
evaluate_hu_preflop_exact_profile(const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
                                  const HuPreflopDecompositionPlan &decomposition,
                                  const HuPreflopWholeGameCoverageAccumulator &coverage,
                                  const HuPreflopAllInBoardCatalog &all_in_catalog,
                                  const HuPreflopAllInEquityTable &all_in_equity_table) {
  const auto valid_blueprint = validate_hu_preflop_blueprint(tree, blueprint);
  const auto valid_coverage =
      validate_hu_preflop_whole_game_coverage_accumulator(tree, decomposition, coverage);
  const auto valid_table =
      validate_hu_preflop_all_in_equity_table(all_in_catalog, all_in_equity_table);
  if (!valid_blueprint || !valid_coverage || !valid_table || coverage.blueprint_iterations == 0U ||
      coverage.validated_boundary_count !=
          static_cast<std::uint64_t>(coverage.task_side_coverage.size()) * 2U ||
      std::ranges::any_of(coverage.task_side_coverage,
                          [](const auto mask) { return mask != 0x3U; })) {
    return Result<HuPreflopExactProfileEvaluation, HuPreflopError>::failure(
        !valid_blueprint ? valid_blueprint.error()
                         : (!valid_coverage ? valid_coverage.error()
                                            : (!valid_table ? valid_table.error()
                                                            : HuPreflopError::IntegrityFailure)));
  }

  HuPreflopExactProfileEvaluation result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = blueprint.fingerprint;
  result.plan_fingerprint = decomposition.fingerprint;
  result.continuation_profile_fingerprint = fingerprint_hu_preflop_continuation_profile(coverage);
  result.all_in_equity_table_fingerprint = all_in_equity_table.fingerprint;
  result.blueprint_iterations = coverage.blueprint_iterations;
  if (!checked_multiply(decomposition.physical_private_deals,
                        ordered_public_runouts_per_private_deal, result.chance_outcome_count)) {
    return Result<HuPreflopExactProfileEvaluation, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  result.postflop_boundary_count = coverage.validated_boundary_count;
  for (std::size_t task = 0U; task < coverage.task_side_coverage.size(); ++task) {
    result.postflop_values_antes[1] += coverage.task_side_profile_utility_antes[task * 2U];
    result.postflop_values_antes[0] += coverage.task_side_profile_utility_antes[task * 2U + 1U];
  }

  const auto terminal_profile = evaluate_hu_preflop_exact_terminal_profile_validated(
      tree, blueprint, all_in_equity_table, result.continuation_profile_fingerprint,
      result.blueprint_iterations, result.chance_outcome_count);
  if (!terminal_profile) {
    return Result<HuPreflopExactProfileEvaluation, HuPreflopError>::failure(
        terminal_profile.error());
  }
  result.terminal_fold_count = terminal_profile.value().fold_count;
  result.terminal_all_in_count = terminal_profile.value().all_in_count;
  for (std::size_t player = 0U; player < 2U; ++player) {
    result.preflop_terminal_values_antes[player] = terminal_profile.value().values_antes[player];
    result.total_values_antes[player] =
        result.postflop_values_antes[player] + result.preflop_terminal_values_antes[player];
  }
  result.fingerprint = exact_profile_evaluation_fingerprint(result);
  const auto valid = validate_hu_preflop_exact_profile_evaluation(
      tree, blueprint, decomposition, coverage, all_in_catalog, all_in_equity_table, result);
  return valid ? Result<HuPreflopExactProfileEvaluation, HuPreflopError>::success(std::move(result))
               : Result<HuPreflopExactProfileEvaluation, HuPreflopError>::failure(valid.error());
}

Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>
build_hu_preflop_global_best_response_evidence(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopWholeGameCoverageAccumulator &coverage,
    const HuPreflopAllInBoardCatalog &all_in_catalog,
    const HuPreflopAllInEquityTable &all_in_equity_table,
    const std::span<const HuPreflopBestResponsePreflopEvaluation> best_responses,
    const HuPreflopExactProfileEvaluation &exact_profile) {
  const auto valid_profile = validate_hu_preflop_exact_profile_evaluation(
      tree, blueprint, decomposition, coverage, all_in_catalog, all_in_equity_table, exact_profile);
  if (!valid_profile || best_responses.size() != 2U || tree.fingerprint.empty() ||
      decomposition.tree_fingerprint != tree.fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      decomposition.physical_private_deals != physical_private_deals) {
    return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
        !valid_profile ? valid_profile.error() : HuPreflopError::InvalidConfiguration);
  }

  std::uint64_t chance_outcome_count = 0U;
  if (!checked_multiply(decomposition.physical_private_deals,
                        ordered_public_runouts_per_private_deal, chance_outcome_count) ||
      chance_outcome_count == 0U) {
    return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }

  std::array<const HuPreflopBestResponsePreflopEvaluation *, 2> response_by_player{};
  std::string continuation_fingerprint;
  std::uint64_t blueprint_iterations = 0U;
  for (const auto &evaluation : best_responses) {
    const auto valid =
        validate_hu_preflop_best_response_preflop_evaluation(tree, decomposition, evaluation);
    if (!valid || evaluation.responding_player > 1U ||
        response_by_player[evaluation.responding_player] != nullptr ||
        evaluation.blueprint_fingerprint != decomposition.blueprint_fingerprint) {
      return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
          !valid ? valid.error() : HuPreflopError::IntegrityFailure);
    }
    if (continuation_fingerprint.empty()) {
      continuation_fingerprint = evaluation.continuation_fingerprint;
      blueprint_iterations = evaluation.blueprint_iterations;
    } else if (evaluation.continuation_fingerprint != continuation_fingerprint ||
               evaluation.blueprint_iterations != blueprint_iterations) {
      return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    response_by_player[evaluation.responding_player] = &evaluation;
  }
  if (response_by_player[0] == nullptr || response_by_player[1] == nullptr ||
      continuation_fingerprint.empty() || blueprint_iterations == 0U ||
      continuation_fingerprint != exact_profile.continuation_profile_fingerprint ||
      blueprint_iterations != exact_profile.blueprint_iterations) {
    return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  const auto combos = all_combos();
  std::array<std::uint64_t, hu_preflop_hand_class_count> combo_count_by_class{};
  for (const auto &combo : combos) {
    ++combo_count_by_class[hand_class(combo)];
  }
  constexpr std::uint64_t compatible_opponent_combos = 561U;
  std::array<double, 2> best_response_values{};
  for (std::size_t player = 0U; player < 2U; ++player) {
    HuPreflopCompensatedCounterfactualValue accumulated;
    for (const auto &value : response_by_player[player]->values) {
      const auto expected_reach =
          static_cast<double>(combo_count_by_class[value.responding_class]) *
          static_cast<double>(compatible_opponent_combos) *
          static_cast<double>(ordered_public_runouts_per_private_deal);
      const auto tolerance = 1.0e-12 * std::max(1.0, std::abs(expected_reach));
      if (std::abs(value.weighted_counterfactual_reach - expected_reach) > tolerance) {
        return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      compensated_add(value.weighted_counterfactual_utility_antes,
                      accumulated.weighted_utility_sum_antes,
                      accumulated.weighted_utility_compensation_antes);
    }
    const auto total_utility =
        accumulated.weighted_utility_sum_antes + accumulated.weighted_utility_compensation_antes;
    best_response_values[player] = total_utility / static_cast<double>(chance_outcome_count);
    if (!std::isfinite(best_response_values[player]) ||
        !std::isfinite(exact_profile.total_values_antes[player])) {
      return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
  }

  HuPreflopGlobalBestResponseEvidence result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  result.continuation_profile_fingerprint = std::move(continuation_fingerprint);
  result.profile_evaluation_fingerprint = exact_profile.fingerprint;
  result.method = "exact_enumerated_whole_game_best_response_v1";
  result.blueprint_iterations = blueprint_iterations;
  result.chance_outcome_count = chance_outcome_count;
  result.best_response_values_antes = best_response_values;
  result.profile_values_antes = exact_profile.total_values_antes;
  for (std::size_t player = 0U; player < 2U; ++player) {
    auto gain = best_response_values[player] - exact_profile.total_values_antes[player];
    const auto tolerance = 1.0e-10 * std::max({1.0, std::abs(best_response_values[player]),
                                               std::abs(exact_profile.total_values_antes[player])});
    if (gain < -tolerance) {
      return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    if (gain < 0.0) {
      gain = 0.0;
    }
    result.deviation_gains_antes[player] = gain;
    result.nashconv_antes += gain;
  }
  const auto stack_antes = static_cast<double>(tree.config.effective_stack.units()) /
                           static_cast<double>(Money::units_per_ante);
  if (!std::isfinite(stack_antes) || stack_antes <= 0.0 || !std::isfinite(result.nashconv_antes)) {
    return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  result.normalized_nashconv = result.nashconv_antes / stack_antes;
  result.exact_best_response = true;
  result.fingerprint = fingerprint_hu_preflop_global_best_response(result);
  return Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>::success(std::move(result));
}

Result<bool, HuPreflopError> validate_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopUpperStreetTerminalContribution &contribution) {
  if (!valid_upper_terminal_contribution_payload(tree, decomposition, contribution)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto shapes = enumerate_hu_postflop_upper_street_terminal_shapes(tree);
  if (!shapes) {
    return Result<bool, HuPreflopError>::failure(shapes.error());
  }
  std::uint64_t ordinal = 0U;
  for (const auto &shape : shapes.value()) {
    if (shape.entry_node != contribution.entry_node) {
      continue;
    }
    if (ordinal == contribution.terminal_ordinal) {
      return shape.terminal_ordinal == ordinal && shape.street == contribution.terminal_street &&
                     shape.state.status == contribution.terminal_status &&
                     shape.history_fingerprint == contribution.terminal_history_fingerprint
                 ? Result<bool, HuPreflopError>::success(true)
                 : Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    ++ordinal;
  }
  return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

Result<bool, HuPreflopError> validate_hu_preflop_upper_street_best_response_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog,
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution) {
  if (!valid_upper_best_response_terminal_contribution_payload(tree, decomposition, catalog,
                                                               contribution)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto shapes = enumerate_hu_postflop_upper_street_terminal_shapes(tree);
  if (!shapes) {
    return Result<bool, HuPreflopError>::failure(shapes.error());
  }
  const auto shape = std::ranges::find_if(shapes.value(), [&](const auto &candidate) {
    return candidate.entry_node == contribution.entry_node &&
           candidate.terminal_ordinal == contribution.terminal_ordinal;
  });
  return shape != shapes.value().end() && shape->street == contribution.terminal_street &&
                 shape->state.status == contribution.terminal_status &&
                 shape->history_fingerprint == contribution.terminal_history_fingerprint
             ? Result<bool, HuPreflopError>::success(true)
             : Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

Result<HuPreflopFlopTaskAccumulator, HuPreflopError>
make_hu_preflop_flop_task_accumulator(const HuPreflopTree &tree,
                                      const HuPreflopDecompositionPlan &decomposition,
                                      const HuPreflopRiverTaskAggregate &river_aggregate) {
  const auto valid_aggregate = validate_hu_preflop_river_task_aggregate(river_aggregate);
  const auto task =
      whole_game_task_index(decomposition, river_aggregate.entry_node, river_aggregate.flop);
  const auto shapes = enumerate_hu_postflop_upper_street_terminal_shapes(tree);
  if (!valid_aggregate || !shapes || tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      river_aggregate.tree_fingerprint != tree.fingerprint ||
      river_aggregate.blueprint_fingerprint != decomposition.blueprint_fingerprint ||
      river_aggregate.value_mode != HuPreflopContinuationValueMode::AverageStrategy ||
      task == std::numeric_limits<std::size_t>::max() || river_aggregate.task_span_index != task) {
    return Result<HuPreflopFlopTaskAccumulator, HuPreflopError>::failure(
        !valid_aggregate ? valid_aggregate.error()
        : !shapes        ? shapes.error()
                         : HuPreflopError::InvalidConfiguration);
  }

  HuPreflopFlopTaskAccumulator result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  result.river_aggregate_fingerprint = river_aggregate.fingerprint;
  result.continuation_checkpoint_fingerprint = river_aggregate.continuation_checkpoint_fingerprint;
  result.blueprint_iterations = river_aggregate.blueprint_iterations;
  result.task_span_index = river_aggregate.task_span_index;
  result.entry_node = river_aggregate.entry_node;
  result.flop = river_aggregate.flop;
  for (const auto &shape : shapes.value()) {
    if (shape.entry_node == result.entry_node) {
      if (shape.terminal_ordinal != result.expected_terminal_fingerprints.size()) {
        return Result<HuPreflopFlopTaskAccumulator, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      result.expected_terminal_fingerprints.push_back(upper_terminal_manifest_fingerprint(
          shape.entry_node, shape.street, shape.state.status, shape.history_fingerprint));
    }
  }
  if (result.expected_terminal_fingerprints.empty()) {
    return Result<HuPreflopFlopTaskAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    for (const auto &source : river_aggregate.resolver_values[resolver]) {
      auto &target = result.values[resolver][source.opponent_combo];
      target.weighted_reach_sum = source.weighted_counterfactual_reach;
      target.weighted_utility_sum_antes = source.weighted_counterfactual_utility_antes;
    }
  }
  result.contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a("gtosd.hu_preflop_flop_task_contributions.v2|" + river_aggregate.fingerprint));
  result.numeric_state_hash = flop_task_numeric_state_hash(result);
  result.fingerprint = flop_task_accumulator_fingerprint(result);
  return valid_flop_task_accumulator_payload(tree, decomposition, result)
             ? Result<HuPreflopFlopTaskAccumulator, HuPreflopError>::success(std::move(result))
             : Result<HuPreflopFlopTaskAccumulator, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<bool, HuPreflopError>
validate_hu_preflop_flop_task_accumulator(const HuPreflopTree &tree,
                                          const HuPreflopDecompositionPlan &decomposition,
                                          const HuPreflopFlopTaskAccumulator &accumulator) {
  if (!valid_flop_task_accumulator_payload(tree, decomposition, accumulator)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto shapes = enumerate_hu_postflop_upper_street_terminal_shapes(tree);
  if (!shapes) {
    return Result<bool, HuPreflopError>::failure(shapes.error());
  }
  std::vector<std::string> expected;
  for (const auto &shape : shapes.value()) {
    if (shape.entry_node == accumulator.entry_node) {
      if (shape.terminal_ordinal != expected.size()) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      expected.push_back(upper_terminal_manifest_fingerprint(
          shape.entry_node, shape.street, shape.state.status, shape.history_fingerprint));
    }
  }
  if (expected != accumulator.expected_terminal_fingerprints) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<bool, HuPreflopError> accumulate_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    HuPreflopFlopTaskAccumulator &accumulator,
    const HuPreflopUpperStreetTerminalContribution &contribution) {
  const auto valid_accumulator =
      valid_flop_task_accumulator_payload(tree, decomposition, accumulator);
  const auto valid_contribution =
      valid_upper_terminal_contribution_payload(tree, decomposition, contribution);
  if (!valid_accumulator || !valid_contribution || accumulator.complete ||
      contribution.tree_fingerprint != accumulator.tree_fingerprint ||
      contribution.blueprint_fingerprint != accumulator.blueprint_fingerprint ||
      contribution.continuation_checkpoint_fingerprint !=
          accumulator.continuation_checkpoint_fingerprint ||
      contribution.blueprint_iterations != accumulator.blueprint_iterations ||
      contribution.task_span_index != accumulator.task_span_index ||
      contribution.entry_node != accumulator.entry_node || contribution.flop != accumulator.flop ||
      contribution.terminal_ordinal != accumulator.next_terminal_ordinal ||
      upper_terminal_manifest_fingerprint(contribution.entry_node, contribution.terminal_street,
                                          contribution.terminal_status,
                                          contribution.terminal_history_fingerprint) !=
          accumulator.expected_terminal_fingerprints[accumulator.next_terminal_ordinal]) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }

  auto candidate = accumulator;
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    for (const auto &source : contribution.resolver_values[resolver]) {
      auto &target = candidate.values[resolver][source.opponent_combo];
      compensated_add(source.weighted_counterfactual_reach, target.weighted_reach_sum,
                      target.weighted_reach_compensation);
      compensated_add(source.weighted_counterfactual_utility_antes,
                      target.weighted_utility_sum_antes,
                      target.weighted_utility_compensation_antes);
    }
  }
  ++candidate.next_terminal_ordinal;
  candidate.complete =
      candidate.next_terminal_ordinal == candidate.expected_terminal_fingerprints.size();
  candidate.contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a(candidate.contribution_chain_fingerprint + "|" + contribution.fingerprint));
  candidate.numeric_state_hash = flop_task_numeric_state_hash(candidate);
  candidate.fingerprint = flop_task_accumulator_fingerprint(candidate);
  const auto valid_candidate = valid_flop_task_accumulator_payload(tree, decomposition, candidate);
  if (!valid_candidate) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  accumulator = std::move(candidate);
  return Result<bool, HuPreflopError>::success(true);
}

Result<std::array<HuPreflopFlopBoundary, 2>, HuPreflopError>
finalize_hu_preflop_flop_task_accumulator(const HuPreflopTree &tree,
                                          const HuPreflopDecompositionPlan &decomposition,
                                          const HuPreflopFlopTaskAccumulator &accumulator) {
  const auto valid = validate_hu_preflop_flop_task_accumulator(tree, decomposition, accumulator);
  const auto *entry = find_entry(decomposition, accumulator.entry_node);
  const auto flop_catalog = std::ranges::find(decomposition.canonical_flop_catalog,
                                              accumulator.flop, &HuPreflopCanonicalFlop::cards);
  if (!valid || !accumulator.complete || entry == nullptr ||
      flop_catalog == decomposition.canonical_flop_catalog.end()) {
    return Result<std::array<HuPreflopFlopBoundary, 2>, HuPreflopError>::failure(
        !valid ? valid.error() : HuPreflopError::InvalidConfiguration);
  }

  const auto combos = all_combos();
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(accumulator.flop);
  std::array<HuPreflopComboReach, 2> cfvs{};
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    std::array<double, 36U> resolver_mass_by_card{};
    double resolver_total = 0.0;
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & board_mask) != 0U) {
        continue;
      }
      const auto reach = entry->own_sequence_reach[resolver][combo];
      resolver_total += reach;
      resolver_mass_by_card[combos[combo].first.value()] += reach;
      resolver_mass_by_card[combos[combo].second.value()] += reach;
    }
    for (std::size_t opponent_combo = 0U; opponent_combo < masks.size(); ++opponent_combo) {
      if ((masks[opponent_combo] & board_mask) != 0U) {
        continue;
      }
      const auto expected_reach =
          (resolver_total - resolver_mass_by_card[combos[opponent_combo].first.value()] -
           resolver_mass_by_card[combos[opponent_combo].second.value()] +
           entry->own_sequence_reach[resolver][opponent_combo]) *
          812.0 * static_cast<double>(flop_catalog->physical_outcome_count);
      const auto &source = accumulator.values[resolver][opponent_combo];
      const auto actual_reach = source.weighted_reach_sum + source.weighted_reach_compensation;
      const auto utility =
          source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
      const auto tolerance = 1.0e-10 * std::max(1.0, std::abs(expected_reach));
      if (!std::isfinite(expected_reach) || expected_reach < 0.0 || !std::isfinite(actual_reach) ||
          actual_reach < 0.0 || !std::isfinite(utility) ||
          std::abs(actual_reach - expected_reach) > tolerance ||
          (actual_reach == 0.0 && utility != 0.0)) {
        return Result<std::array<HuPreflopFlopBoundary, 2>, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      cfvs[resolver][opponent_combo] = actual_reach > 0.0 ? utility / actual_reach : 0.0;
    }
  }

  const auto conditioned =
      condition_hu_preflop_ranges_on_flop(decomposition, accumulator.entry_node, accumulator.flop);
  if (!conditioned) {
    return Result<std::array<HuPreflopFlopBoundary, 2>, HuPreflopError>::failure(
        conditioned.error());
  }
  const auto continuation_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a("gtosd.hu_preflop_assembled_flop_continuation.v1|" + accumulator.fingerprint));
  std::array<HuPreflopFlopBoundary, 2> result;
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    const auto boundary = build_hu_preflop_flop_boundary(
        tree, decomposition, conditioned.value(), resolver, cfvs[resolver],
        continuation_fingerprint, accumulator.blueprint_iterations,
        accumulator.continuation_checkpoint_fingerprint);
    if (!boundary) {
      return Result<std::array<HuPreflopFlopBoundary, 2>, HuPreflopError>::failure(
          boundary.error());
    }
    result[resolver] = boundary.value();
  }
  return Result<std::array<HuPreflopFlopBoundary, 2>, HuPreflopError>::success(std::move(result));
}

Result<HuPreflopRiverWorkEstimate, HuPreflopError>
estimate_hu_preflop_river_work(const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan) {
  if (tree.fingerprint != plan.tree_fingerprint || plan.canonical_flop_catalog.empty() ||
      plan.fingerprint != fingerprint_hu_preflop_decomposition_plan(plan)) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto river_shapes = enumerate_hu_postflop_betting_root_shapes(tree, Street::River);
  if (!river_shapes) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(river_shapes.error());
  }
  const auto all_in_catalog = build_hu_preflop_all_in_board_catalog();
  if (!all_in_catalog) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(all_in_catalog.error());
  }

  HuPreflopRiverWorkEstimate result;
  result.physical_ordered_runouts_per_flop = 33U * 32U;
  result.minimum_canonical_runouts_per_flop = std::numeric_limits<std::uint64_t>::max();
  for (const auto &flop : plan.canonical_flop_catalog) {
    const auto runouts = canonical_ordered_runouts(flop.cards);
    if (!runouts) {
      return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(runouts.error());
    }
    const auto runout_count = static_cast<std::uint64_t>(runouts.value().size());
    result.canonical_public_board_histories += runout_count;
    result.minimum_canonical_runouts_per_flop =
        std::min(result.minimum_canonical_runouts_per_flop, runout_count);
    result.maximum_canonical_runouts_per_flop =
        std::max(result.maximum_canonical_runouts_per_flop, runout_count);
    std::array<std::uint64_t, 36U> river_roots_by_physical_turn{};
    for (const auto &runout : runouts.value()) {
      const auto lifts = ordered_runout_orbit_lifts(flop.cards, runout.cards[0], runout.cards[1]);
      if (!lifts) {
        return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(lifts.error());
      }
      std::array<bool, 36U> represented_turns{};
      for (const auto &permutation : lifts.value()) {
        const auto canonical_turn = runout.cards[0];
        const auto physical_turn = static_cast<std::uint8_t>(
            (canonical_turn.value() / 4U) * 4U + permutation[canonical_turn.value() % 4U]);
        represented_turns[physical_turn] = true;
      }
      for (std::size_t turn = 0U; turn < represented_turns.size(); ++turn) {
        if (represented_turns[turn]) {
          ++river_roots_by_physical_turn[turn];
        }
      }
    }
    result.maximum_canonical_river_roots_per_physical_turn_leaf =
        std::max(result.maximum_canonical_river_roots_per_physical_turn_leaf,
                 *std::ranges::max_element(river_roots_by_physical_turn));
    const auto stabilizer = flop_suit_stabilizer(flop.cards);
    const auto board_mask = flop_mask(flop.cards);
    for (std::uint8_t turn = 0U; turn < 36U; ++turn) {
      const auto turn_card = CardId::from_index(turn);
      if (!turn_card || (turn_card.value().mask() & board_mask) != 0U) {
        continue;
      }
      std::array<bool, 36U> physical_turns{};
      for (const auto &permutation : stabilizer) {
        const auto physical_turn =
            static_cast<std::uint8_t>((turn / 4U) * 4U + permutation[turn % 4U]);
        physical_turns[physical_turn] = true;
      }
      result.maximum_best_response_turn_components_per_root =
          std::max(result.maximum_best_response_turn_components_per_root,
                   static_cast<std::uint64_t>(
                       std::count(physical_turns.begin(), physical_turns.end(), true)));
    }
  }
  if (!checked_multiply(plan.physical_flops, result.physical_ordered_runouts_per_flop,
                        result.physical_public_board_histories)) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  result.river_betting_histories = river_shapes.value().size();
  if (!checked_multiply(result.river_betting_histories, result.physical_public_board_histories,
                        result.physical_resolver_roots) ||
      !checked_multiply(result.river_betting_histories, result.canonical_public_board_histories,
                        result.canonical_resolver_roots) ||
      !checked_multiply(result.canonical_resolver_roots, 2U,
                        result.canonical_roots_for_both_players)) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  // An orbit-lifted canonical River result can touch every combo that is live
  // on the parent Flop, even when that combo is blocked on the representative
  // River board. Reserving only the 465 representative-board combos loses the
  // private-card permutation required by exact suit isomorphism.
  result.boundary_values_per_resolver_root = live_combos_per_flop;
  if (result.maximum_best_response_turn_components_per_root == 0U ||
      result.maximum_best_response_turn_components_per_root ==
          std::numeric_limits<std::uint64_t>::max() ||
      result.maximum_canonical_river_roots_per_physical_turn_leaf == 0U) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto best_response_row_sets = result.maximum_best_response_turn_components_per_root + 1U;
  if (!checked_multiply(result.boundary_values_per_resolver_root, dense_boundary_record_bytes,
                        result.boundary_bytes_per_resolver_root) ||
      !checked_multiply(result.canonical_roots_for_both_players,
                        result.boundary_bytes_per_resolver_root,
                        result.fully_materialized_boundary_bytes) ||
      !checked_multiply(result.boundary_bytes_per_resolver_root, best_response_row_sets,
                        result.best_response_boundary_bytes_per_resolver_root) ||
      !checked_multiply(result.canonical_roots_for_both_players,
                        result.best_response_boundary_bytes_per_resolver_root,
                        result.fully_materialized_best_response_boundary_bytes) ||
      !checked_multiply(630U,
                        static_cast<std::uint64_t>(sizeof(HuPreflopCompensatedCounterfactualValue)),
                        result.best_response_leaf_accumulator_value_bytes) ||
      !checked_multiply(result.maximum_canonical_river_roots_per_physical_turn_leaf,
                        static_cast<std::uint64_t>(sizeof(std::uint64_t)),
                        result.maximum_best_response_leaf_manifest_bytes) ||
      !checked_add(result.best_response_leaf_accumulator_value_bytes,
                   result.maximum_best_response_leaf_manifest_bytes,
                   result.maximum_best_response_leaf_live_payload_bytes) ||
      !checked_add(result.maximum_best_response_leaf_live_payload_bytes,
                   result.best_response_boundary_bytes_per_resolver_root,
                   result.maximum_best_response_leaf_live_payload_bytes) ||
      !checked_multiply(live_combos_per_flop,
                        static_cast<std::uint64_t>(sizeof(HuPreflopBestResponseComboValue)),
                        result.best_response_task_value_bytes) ||
      !checked_multiply(hu_preflop_hand_class_count,
                        static_cast<std::uint64_t>(sizeof(HuPreflopCompensatedCounterfactualValue)),
                        result.best_response_entry_accumulator_value_bytes) ||
      !checked_add(result.best_response_task_value_bytes,
                   result.best_response_entry_accumulator_value_bytes,
                   result.best_response_entry_reduction_live_payload_bytes)) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  result.all_in_canonical_complete_boards = all_in_catalog.value().boards.size();
  std::uint64_t all_in_evaluated_hand_bytes = 0U;
  std::uint64_t all_in_live_combo_index_bytes = 0U;
  std::uint64_t all_in_combo_bytes = 0U;
  std::uint64_t all_in_combo_mask_bytes = 0U;
  std::uint64_t two_matchup_tables = 0U;
  if (!checked_multiply(result.all_in_canonical_complete_boards,
                        static_cast<std::uint64_t>(sizeof(HuPreflopCanonicalAllInBoard)),
                        result.all_in_board_catalog_payload_bytes) ||
      !checked_multiply(all_in_matchup_count,
                        static_cast<std::uint64_t>(sizeof(HuPreflopAllInMatchupCount)),
                        result.all_in_matchup_table_payload_bytes) ||
      !checked_multiply(630U, static_cast<std::uint64_t>(sizeof(HandValue)),
                        all_in_evaluated_hand_bytes) ||
      !checked_multiply(live_private_combos_per_complete_board,
                        static_cast<std::uint64_t>(sizeof(std::size_t)),
                        all_in_live_combo_index_bytes) ||
      !checked_multiply(630U, static_cast<std::uint64_t>(sizeof(Combo)), all_in_combo_bytes) ||
      !checked_multiply(630U, static_cast<std::uint64_t>(sizeof(std::uint64_t)),
                        all_in_combo_mask_bytes) ||
      !checked_multiply(result.all_in_matchup_table_payload_bytes, 2U, two_matchup_tables) ||
      !checked_add(two_matchup_tables, all_in_evaluated_hand_bytes,
                   result.all_in_one_board_scratch_payload_bytes) ||
      !checked_add(result.all_in_one_board_scratch_payload_bytes, all_in_live_combo_index_bytes,
                   result.all_in_one_board_scratch_payload_bytes) ||
      !checked_add(result.all_in_one_board_scratch_payload_bytes, all_in_combo_bytes,
                   result.all_in_one_board_scratch_payload_bytes) ||
      !checked_add(result.all_in_one_board_scratch_payload_bytes, all_in_combo_mask_bytes,
                   result.all_in_one_board_scratch_payload_bytes) ||
      !checked_add(result.all_in_board_catalog_payload_bytes,
                   result.all_in_matchup_table_payload_bytes,
                   result.all_in_streaming_live_payload_bytes) ||
      !checked_add(result.all_in_streaming_live_payload_bytes,
                   result.all_in_one_board_scratch_payload_bytes,
                   result.all_in_streaming_live_payload_bytes)) {
    return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  result.fingerprint = river_work_fingerprint(tree.fingerprint, plan.fingerprint, result);
  return Result<HuPreflopRiverWorkEstimate, HuPreflopError>::success(std::move(result));
}

Result<HuPreflopRiverBatchPlan, HuPreflopError> derive_hu_preflop_river_batch_plan(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopRiverWorkEstimate &work, const HuPreflopRiverRootCatalog &catalog,
    const std::uint64_t target_batch_payload_bytes) {
  if (tree.fingerprint != plan.tree_fingerprint || plan.blueprint_fingerprint.empty() ||
      plan.fingerprint != fingerprint_hu_preflop_decomposition_plan(plan) ||
      work.fingerprint != river_work_fingerprint(tree.fingerprint, plan.fingerprint, work) ||
      !valid_river_root_catalog(catalog) || catalog.tree_fingerprint != tree.fingerprint ||
      catalog.decomposition_plan_fingerprint != plan.fingerprint ||
      catalog.river_work_fingerprint != work.fingerprint ||
      work.canonical_roots_for_both_players == 0U || work.boundary_bytes_per_resolver_root == 0U ||
      target_batch_payload_bytes < work.boundary_bytes_per_resolver_root ||
      target_batch_payload_bytes >= work.fully_materialized_boundary_bytes) {
    return Result<HuPreflopRiverBatchPlan, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  HuPreflopRiverBatchPlan result;
  result.tree_fingerprint = tree.fingerprint;
  result.blueprint_fingerprint = plan.blueprint_fingerprint;
  result.decomposition_plan_fingerprint = plan.fingerprint;
  result.river_work_fingerprint = work.fingerprint;
  result.resolver_root_ordering =
      "entry/flop_lex/river_shape_dfs/canonical_ordered_runout/resolver_pair_v3";
  result.resolver_roots = work.canonical_roots_for_both_players;
  result.target_batch_payload_bytes = target_batch_payload_bytes;
  result.boundary_bytes_per_resolver_root = work.boundary_bytes_per_resolver_root;
  result.roots_per_batch = target_batch_payload_bytes / work.boundary_bytes_per_resolver_root;
  result.roots_per_batch -= result.roots_per_batch % 2U;
  if (result.roots_per_batch == 0U) {
    return Result<HuPreflopRiverBatchPlan, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  result.fully_materialized_boundary_bytes = work.fully_materialized_boundary_bytes;
  std::uint64_t maximum_batch_roots = 0U;
  for (std::size_t task_index = 0U; task_index < catalog.task_spans.size(); ++task_index) {
    const auto &span = catalog.task_spans[task_index];
    auto first = span.first_resolver_root;
    auto remaining = span.resolver_root_count;
    while (remaining > 0U) {
      const auto count = std::min(result.roots_per_batch, remaining);
      result.batch_first_resolver_roots.push_back(first);
      result.batch_resolver_root_counts.push_back(count);
      result.batch_task_span_indices.push_back(static_cast<std::uint64_t>(task_index));
      maximum_batch_roots = std::max(maximum_batch_roots, count);
      first += count;
      remaining -= count;
    }
  }
  result.batch_count = result.batch_resolver_root_counts.size();
  if (result.batch_count == 0U) {
    return Result<HuPreflopRiverBatchPlan, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  result.final_batch_root_count = result.batch_resolver_root_counts.back();
  if (!checked_multiply(maximum_batch_roots, result.boundary_bytes_per_resolver_root,
                        result.maximum_batch_payload_bytes)) {
    return Result<HuPreflopRiverBatchPlan, HuPreflopError>::failure(HuPreflopError::CountOverflow);
  }
  result.fingerprint = river_batch_plan_fingerprint(result);
  return valid_river_batch_plan(result)
             ? Result<HuPreflopRiverBatchPlan, HuPreflopError>::success(std::move(result))
             : Result<HuPreflopRiverBatchPlan, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<HuPreflopRiverBatch, HuPreflopError>
hu_preflop_river_batch_at(const HuPreflopRiverBatchPlan &plan, const std::uint64_t batch_index) {
  if (!valid_river_batch_plan(plan) || batch_index >= plan.batch_count) {
    return Result<HuPreflopRiverBatch, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  HuPreflopRiverBatch result;
  result.index = batch_index;
  result.task_span_index = plan.batch_task_span_indices[batch_index];
  result.first_resolver_root = plan.batch_first_resolver_roots[batch_index];
  result.resolver_root_count = plan.batch_resolver_root_counts[batch_index];
  if (!checked_multiply(result.resolver_root_count, plan.boundary_bytes_per_resolver_root,
                        result.maximum_boundary_payload_bytes)) {
    return Result<HuPreflopRiverBatch, HuPreflopError>::failure(HuPreflopError::CountOverflow);
  }
  result.batch_plan_fingerprint = plan.fingerprint;
  result.fingerprint = river_batch_fingerprint(result);
  return Result<HuPreflopRiverBatch, HuPreflopError>::success(std::move(result));
}

Result<HuPreflopRiverResolverRoot, HuPreflopError> hu_preflop_river_resolver_root_at(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverWorkEstimate &work, const HuPreflopRiverBatchPlan &batch_plan,
    const std::uint64_t resolver_root_ordinal) {
  const auto catalog = build_hu_preflop_river_root_catalog(tree, decomposition, work);
  return catalog
             ? hu_preflop_river_resolver_root_at(catalog.value(), batch_plan, resolver_root_ordinal)
             : Result<HuPreflopRiverResolverRoot, HuPreflopError>::failure(catalog.error());
}

Result<HuPreflopRiverRootCatalog, HuPreflopError>
build_hu_preflop_river_root_catalog(const HuPreflopTree &tree,
                                    const HuPreflopDecompositionPlan &decomposition,
                                    const HuPreflopRiverWorkEstimate &work) {
  if (tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      work.fingerprint !=
          river_work_fingerprint(tree.fingerprint, decomposition.fingerprint, work)) {
    return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto shapes = enumerate_hu_postflop_betting_root_shapes(tree, Street::River);
  if (!shapes || shapes.value().size() != work.river_betting_histories) {
    return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  HuPreflopRiverRootCatalog result;
  result.tree_fingerprint = tree.fingerprint;
  result.decomposition_plan_fingerprint = decomposition.fingerprint;
  result.river_work_fingerprint = work.fingerprint;
  result.physical_public_board_histories = work.physical_public_board_histories;
  // Group shapes by postflop entry. This is the first dimension in the
  // execution order, so every (entry, Flop) task owns one contiguous span.
  struct EntryShapeSpan {
    std::uint32_t entry_node{0};
    std::uint64_t first{0};
    std::uint64_t count{0};
  };
  std::vector<EntryShapeSpan> entry_shape_spans;
  entry_shape_spans.reserve(decomposition.entries.size());
  result.river_shapes.reserve(shapes.value().size());
  for (const auto &entry : decomposition.entries) {
    const auto first = static_cast<std::uint64_t>(result.river_shapes.size());
    for (const auto &shape : shapes.value()) {
      if (shape.entry_node == entry.entry_node) {
        result.river_shapes.push_back(shape);
      }
    }
    const auto count = static_cast<std::uint64_t>(result.river_shapes.size()) - first;
    if (count == 0U) {
      return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    entry_shape_spans.push_back({entry.entry_node, first, count});
  }
  if (result.river_shapes.size() != shapes.value().size()) {
    return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  result.canonical_boards.reserve(static_cast<std::size_t>(work.canonical_public_board_histories));
  struct FlopBoardSpan {
    std::uint32_t flop_catalog_index{0};
    std::uint64_t first{0};
    std::uint64_t count{0};
  };
  std::vector<FlopBoardSpan> flop_board_spans;
  flop_board_spans.reserve(decomposition.canonical_flop_catalog.size());
  std::uint32_t flop_catalog_index = 0U;
  for (const auto &flop : decomposition.canonical_flop_catalog) {
    const auto first = static_cast<std::uint64_t>(result.canonical_boards.size());
    const auto runouts = canonical_ordered_runouts(flop.cards);
    if (!runouts) {
      return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(runouts.error());
    }
    for (const auto &runout : runouts.value()) {
      const auto physical_count =
          static_cast<std::uint64_t>(flop.physical_outcome_count) * runout.physical_outcome_count;
      if (physical_count == 0U || physical_count > std::numeric_limits<std::uint32_t>::max()) {
        return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
            HuPreflopError::CountOverflow);
      }
      result.canonical_boards.push_back({flop.cards, runout.cards[0], runout.cards[1],
                                         static_cast<std::uint32_t>(physical_count)});
    }
    const auto count = static_cast<std::uint64_t>(result.canonical_boards.size()) - first;
    if (count == 0U) {
      return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    flop_board_spans.push_back({flop_catalog_index, first, count});
    ++flop_catalog_index;
  }

  result.task_spans.reserve(decomposition.entries.size() *
                            decomposition.canonical_flop_catalog.size());
  std::uint64_t first_resolver_root = 0U;
  for (const auto &entry_span : entry_shape_spans) {
    for (const auto &flop_span : flop_board_spans) {
      std::uint64_t roots_per_shape = 0U;
      std::uint64_t resolver_root_count = 0U;
      if (!checked_multiply(flop_span.count, 2U, roots_per_shape) ||
          !checked_multiply(entry_span.count, roots_per_shape, resolver_root_count) ||
          first_resolver_root > std::numeric_limits<std::uint64_t>::max() - resolver_root_count) {
        return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
            HuPreflopError::CountOverflow);
      }
      result.task_spans.push_back({first_resolver_root, resolver_root_count, entry_span.entry_node,
                                   flop_span.flop_catalog_index, entry_span.first, entry_span.count,
                                   flop_span.first, flop_span.count});
      first_resolver_root += resolver_root_count;
    }
  }
  result.resolver_roots = work.canonical_roots_for_both_players;
  if (first_resolver_root != result.resolver_roots) {
    return Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  result.fingerprint = river_root_catalog_fingerprint(result);
  return valid_river_root_catalog(result)
             ? Result<HuPreflopRiverRootCatalog, HuPreflopError>::success(std::move(result))
             : Result<HuPreflopRiverRootCatalog, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<std::vector<HuPreflopRiverTurnGroup>, HuPreflopError>
enumerate_hu_preflop_river_turn_groups(const HuPreflopRiverRootCatalog &catalog,
                                       const std::uint64_t task_span_index) {
  if (!valid_river_root_catalog(catalog) || task_span_index >= catalog.task_spans.size()) {
    return Result<std::vector<HuPreflopRiverTurnGroup>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &span = catalog.task_spans[task_span_index];
  const auto &first_board = catalog.canonical_boards[span.first_board_index];
  std::vector<HuPreflopRiverTurnGroup> result;
  std::uint64_t board_offset = 0U;
  std::uint64_t physical_total = 0U;
  while (board_offset < span.board_count) {
    HuPreflopRiverTurnGroup group;
    group.root_catalog_fingerprint = catalog.fingerprint;
    group.task_span_index = task_span_index;
    group.entry_node = span.entry_node;
    group.flop = first_board.flop;
    group.turn = catalog.canonical_boards[span.first_board_index + board_offset].turn;
    group.first_board_offset = board_offset;
    while (board_offset < span.board_count) {
      const auto &board = catalog.canonical_boards[span.first_board_index + board_offset];
      if (board.flop != group.flop || board.turn != group.turn) {
        break;
      }
      if (group.physical_public_outcome_count >
          std::numeric_limits<std::uint64_t>::max() - board.physical_outcome_count) {
        return Result<std::vector<HuPreflopRiverTurnGroup>, HuPreflopError>::failure(
            HuPreflopError::CountOverflow);
      }
      group.physical_public_outcome_count += board.physical_outcome_count;
      ++group.canonical_board_count;
      ++board_offset;
    }
    if (group.canonical_board_count == 0U || group.physical_public_outcome_count == 0U ||
        physical_total >
            std::numeric_limits<std::uint64_t>::max() - group.physical_public_outcome_count) {
      return Result<std::vector<HuPreflopRiverTurnGroup>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    physical_total += group.physical_public_outcome_count;
    group.fingerprint = river_turn_group_fingerprint(group);
    result.push_back(std::move(group));
  }
  const auto expected_physical_total = std::accumulate(
      catalog.canonical_boards.begin() + static_cast<std::ptrdiff_t>(span.first_board_index),
      catalog.canonical_boards.begin() +
          static_cast<std::ptrdiff_t>(span.first_board_index + span.board_count),
      std::uint64_t{0}, [](const std::uint64_t sum, const auto &board) {
        return sum + board.physical_outcome_count;
      });
  return !result.empty() && board_offset == span.board_count &&
                 physical_total == expected_physical_total
             ? Result<std::vector<HuPreflopRiverTurnGroup>, HuPreflopError>::success(
                   std::move(result))
             : Result<std::vector<HuPreflopRiverTurnGroup>, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>
enumerate_hu_preflop_river_turn_group_resolver_spans(const HuPreflopRiverRootCatalog &catalog,
                                                     const HuPreflopRiverTurnGroup &turn_group) {
  if (!valid_river_root_catalog(catalog) ||
      turn_group.root_catalog_fingerprint != catalog.fingerprint ||
      turn_group.task_span_index >= catalog.task_spans.size() ||
      turn_group.fingerprint != river_turn_group_fingerprint(turn_group)) {
    return Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &task = catalog.task_spans[turn_group.task_span_index];
  const auto &first_board = catalog.canonical_boards[task.first_board_index];
  if (turn_group.entry_node != task.entry_node || turn_group.flop != first_board.flop ||
      turn_group.canonical_board_count == 0U || turn_group.first_board_offset >= task.board_count ||
      turn_group.canonical_board_count > task.board_count - turn_group.first_board_offset) {
    return Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto board_begin = task.first_board_index + turn_group.first_board_offset;
  const auto board_end = board_begin + turn_group.canonical_board_count;
  std::uint64_t physical_outcomes = 0U;
  for (auto board_index = board_begin; board_index < board_end; ++board_index) {
    const auto &board = catalog.canonical_boards[board_index];
    if (board.flop != turn_group.flop || board.turn != turn_group.turn ||
        physical_outcomes >
            std::numeric_limits<std::uint64_t>::max() - board.physical_outcome_count) {
      return Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    physical_outcomes += board.physical_outcome_count;
  }
  if (physical_outcomes != turn_group.physical_public_outcome_count) {
    return Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  std::uint64_t roots_per_shape = 0U;
  std::uint64_t roots_per_group_shape = 0U;
  if (!checked_multiply(task.board_count, 2U, roots_per_shape) ||
      !checked_multiply(turn_group.canonical_board_count, 2U, roots_per_group_shape)) {
    return Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  std::vector<HuPreflopRiverTurnResolverSpan> result;
  result.reserve(static_cast<std::size_t>(task.shape_count));
  for (std::uint64_t local_shape = 0U; local_shape < task.shape_count; ++local_shape) {
    std::uint64_t shape_root_offset = 0U;
    std::uint64_t board_root_offset = 0U;
    if (!checked_multiply(local_shape, roots_per_shape, shape_root_offset) ||
        !checked_multiply(turn_group.first_board_offset, 2U, board_root_offset) ||
        task.first_resolver_root > std::numeric_limits<std::uint64_t>::max() - shape_root_offset ||
        task.first_resolver_root + shape_root_offset >
            std::numeric_limits<std::uint64_t>::max() - board_root_offset) {
      return Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
          HuPreflopError::CountOverflow);
    }
    HuPreflopRiverTurnResolverSpan span;
    span.root_catalog_fingerprint = catalog.fingerprint;
    span.turn_group_fingerprint = turn_group.fingerprint;
    span.task_span_index = turn_group.task_span_index;
    span.shape_index = task.first_shape_index + local_shape;
    span.first_resolver_root = task.first_resolver_root + shape_root_offset + board_root_offset;
    span.resolver_root_count = roots_per_group_shape;
    if (span.first_resolver_root < task.first_resolver_root ||
        span.first_resolver_root > task.first_resolver_root + task.resolver_root_count ||
        span.resolver_root_count >
            task.first_resolver_root + task.resolver_root_count - span.first_resolver_root) {
      return Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
          HuPreflopError::CountOverflow);
    }
    span.fingerprint = river_turn_resolver_span_fingerprint(span);
    result.push_back(std::move(span));
  }
  return result.empty()
             ? Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure)
             : Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>::success(
                   std::move(result));
}

Result<HuPreflopRiverResolverRoot, HuPreflopError>
hu_preflop_river_resolver_root_at(const HuPreflopRiverRootCatalog &catalog,
                                  const HuPreflopRiverBatchPlan &batch_plan,
                                  const std::uint64_t resolver_root_ordinal) {
  if (!valid_river_batch_plan_for_catalog(batch_plan, catalog) ||
      resolver_root_ordinal >= catalog.resolver_roots) {
    return Result<HuPreflopRiverResolverRoot, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  return river_resolver_root_at_unchecked(catalog, batch_plan, resolver_root_ordinal);
}

Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>
enumerate_hu_preflop_river_batch_roots(const HuPreflopRiverRootCatalog &catalog,
                                       const HuPreflopRiverBatchPlan &batch_plan,
                                       const HuPreflopRiverBatch &batch) {
  const auto expected = hu_preflop_river_batch_at(batch_plan, batch.index);
  if (!valid_river_batch_plan_for_catalog(batch_plan, catalog) || !expected ||
      expected.value().fingerprint != batch.fingerprint) {
    return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  std::vector<HuPreflopRiverResolverRoot> result;
  result.reserve(static_cast<std::size_t>(batch.resolver_root_count));
  for (std::uint64_t offset = 0U; offset < batch.resolver_root_count; ++offset) {
    const auto root =
        river_resolver_root_at_unchecked(catalog, batch_plan, batch.first_resolver_root + offset);
    if (!root) {
      return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::failure(root.error());
    }
    result.push_back(root.value());
  }
  return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::success(
      std::move(result));
}

Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>
enumerate_hu_preflop_river_board_roots(const HuPreflopRiverRootCatalog &catalog,
                                       const HuPreflopRiverBatchPlan &batch_plan,
                                       const std::uint64_t task_span_index,
                                       const std::uint64_t canonical_board_offset,
                                       const std::uint8_t resolving_player) {
  if (!valid_river_batch_plan_for_catalog(batch_plan, catalog) ||
      task_span_index >= catalog.task_spans.size() || resolving_player > 1U) {
    return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &task = catalog.task_spans[task_span_index];
  if (canonical_board_offset >= task.board_count || task.shape_count == 0U) {
    return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  std::uint64_t roots_per_shape = 0U;
  if (!checked_multiply(task.board_count, 2U, roots_per_shape)) {
    return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::failure(
        HuPreflopError::CountOverflow);
  }
  std::vector<HuPreflopRiverResolverRoot> result;
  result.reserve(static_cast<std::size_t>(task.shape_count));
  for (std::uint64_t local_shape = 0U; local_shape < task.shape_count; ++local_shape) {
    std::uint64_t shape_offset = 0U;
    std::uint64_t board_offset = 0U;
    if (!checked_multiply(local_shape, roots_per_shape, shape_offset) ||
        !checked_multiply(canonical_board_offset, 2U, board_offset) ||
        task.first_resolver_root > std::numeric_limits<std::uint64_t>::max() - shape_offset ||
        task.first_resolver_root + shape_offset >
            std::numeric_limits<std::uint64_t>::max() - board_offset - resolving_player) {
      return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::failure(
          HuPreflopError::CountOverflow);
    }
    const auto ordinal = task.first_resolver_root + shape_offset + board_offset + resolving_player;
    auto root = river_resolver_root_at_unchecked(catalog, batch_plan, ordinal);
    if (!root) {
      return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::failure(root.error());
    }
    result.push_back(std::move(root.value()));
  }
  return Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>::success(
      std::move(result));
}

Result<PostflopTreeConfig, HuPreflopError>
make_hu_preflop_river_postflop_config(const HuPreflopTree &tree,
                                      const HuPreflopRiverResolverRoot &root) {
  if (root.entry_node >= tree.nodes.size() ||
      tree.nodes[root.entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      root.resolving_player > 1U || root.history_fingerprint.empty() ||
      root.batch_plan_fingerprint.empty() ||
      root.fingerprint != river_resolver_root_fingerprint(root) ||
      root.state.street != Street::River || root.state.status != HandStatus::InProgress ||
      root.state.remaining_stacks[0] != root.state.remaining_stacks[1] ||
      std::popcount(root.state.board_mask) != 5) {
    return Result<PostflopTreeConfig, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  auto card_independent_state = root.state;
  card_independent_state.board_mask = 0U;
  const HuPostflopBettingRootShape shape{root.entry_node, Street::River, card_independent_state,
                                         root.action_history, root.history_fingerprint};
  const auto replayed = replay_hu_postflop_betting_root_shape(tree, shape);
  if (!replayed) {
    return Result<PostflopTreeConfig, HuPreflopError>::failure(replayed.error());
  }
  auto config = make_hu_preflop_postflop_config(tree, root.entry_node, root.flop);
  if (!config) {
    return config;
  }
  config.value().turn = root.turn;
  config.value().river = root.river;
  config.value().initial_pot = root.state.pot;
  config.value().effective_stack = root.state.remaining_stacks[0];
  const auto valid = validate_tree_config(config.value());
  return valid ? config
               : Result<PostflopTreeConfig, HuPreflopError>::failure(
                     HuPreflopError::InvalidConfiguration);
}

Result<HuPreflopRiverRootBoundary, HuPreflopError> build_hu_preflop_river_root_boundary(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root, const PostflopRootCounterfactualValues &postflop_values,
    const HuPreflopComboReach &exact_resolving_sequence_reach,
    const HuPreflopComboReach &opponent_postflop_action_sequence_reach,
    std::string continuation_fingerprint) {
  if (tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      root.entry_node >= tree.nodes.size() || root.resolving_player > 1U ||
      root.batch_plan_fingerprint.empty() ||
      root.fingerprint != river_resolver_root_fingerprint(root) ||
      (postflop_values.mode != PostflopRootValueMode::AverageStrategy &&
       postflop_values.mode != PostflopRootValueMode::ExactBestResponse) ||
      postflop_values.game_fingerprint.empty() || postflop_values.blueprint_iterations == 0U ||
      postflop_values.maximum_recomposition_error_antes > 1.0e-9 ||
      !std::isfinite(postflop_values.maximum_recomposition_error_antes) ||
      continuation_fingerprint != postflop_values.game_fingerprint) {
    return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  for (std::size_t combo = 0U; combo < exact_resolving_sequence_reach.size(); ++combo) {
    const auto reach = exact_resolving_sequence_reach[combo];
    const auto opponent_action_reach = opponent_postflop_action_sequence_reach[combo];
    if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(opponent_action_reach) ||
        opponent_action_reach < 0.0 || opponent_action_reach > 1.0) {
      return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
  }

  const auto opponent = static_cast<std::uint8_t>(1U - root.resolving_player);
  const auto &source = postflop_values.players[opponent];
  if (source.size() != river_live_combos_per_board) {
    return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto lifts = ordered_runout_orbit_lifts(root.flop, root.turn, root.river);
  const auto canonical_flop_entry = std::ranges::find(decomposition.canonical_flop_catalog,
                                                      root.flop, &HuPreflopCanonicalFlop::cards);
  std::uint64_t expected_physical_outcome_count = 0U;
  if (!lifts || canonical_flop_entry == decomposition.canonical_flop_catalog.end() ||
      !checked_multiply(static_cast<std::uint64_t>(lifts.value().size()),
                        canonical_flop_entry->physical_outcome_count,
                        expected_physical_outcome_count) ||
      expected_physical_outcome_count != root.physical_public_outcome_count) {
    return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto masks = combo_masks();
  const auto combos = all_combos();
  const auto board_mask = root.state.board_mask;
  const auto parent_flop_mask = flop_mask(root.flop);
  std::array<bool, 630U> observed_source{};
  std::array<double, 630U> source_counterfactual_reach{};
  std::array<double, 36U> resolving_mass_by_card{};
  double resolving_total = 0.0;
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    if ((masks[combo] & board_mask) != 0U) {
      continue;
    }
    const auto reach = exact_resolving_sequence_reach[combo];
    resolving_total += reach;
    resolving_mass_by_card[combos[combo].first.value()] += reach;
    resolving_mass_by_card[combos[combo].second.value()] += reach;
  }
  std::array<double, 630U> orbit_reach_sum{};
  std::array<double, 630U> orbit_reach_compensation{};
  std::array<double, 630U> orbit_utility_sum{};
  std::array<double, 630U> orbit_utility_compensation{};
  struct TurnComponentAccumulator {
    std::array<double, 630U> reach_sum{};
    std::array<double, 630U> reach_compensation{};
    std::array<double, 630U> utility_sum{};
    std::array<double, 630U> utility_compensation{};
  };
  std::map<std::uint8_t, TurnComponentAccumulator> best_response_turn_values;
  HuPreflopRiverRootBoundary boundary;
  boundary.tree_fingerprint = tree.fingerprint;
  boundary.blueprint_fingerprint = decomposition.blueprint_fingerprint;
  boundary.batch_plan_fingerprint = root.batch_plan_fingerprint;
  boundary.resolver_root_fingerprint = root.fingerprint;
  boundary.continuation_fingerprint = std::move(continuation_fingerprint);
  boundary.value_mode = postflop_values.mode == PostflopRootValueMode::ExactBestResponse
                            ? HuPreflopContinuationValueMode::ExactBestResponse
                            : HuPreflopContinuationValueMode::AverageStrategy;
  boundary.blueprint_iterations = postflop_values.blueprint_iterations;
  boundary.resolver_root_ordinal = root.ordinal;
  boundary.task_span_index = root.task_span_index;
  boundary.entry_node = root.entry_node;
  boundary.flop = root.flop;
  boundary.turn = root.turn;
  boundary.river = root.river;
  boundary.resolving_player = root.resolving_player;
  boundary.opponent = opponent;
  boundary.physical_public_outcome_count = root.physical_public_outcome_count;
  boundary.values.reserve(live_combos_per_flop);
  for (const auto &value : source) {
    if (value.combo >= masks.size() || observed_source[value.combo] ||
        (masks[value.combo] & board_mask) != 0U || !value.positive_reach ||
        !std::isfinite(value.conditional_value_antes)) {
      return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto opponent_combo = combos[value.combo];
    const auto counterfactual_reach = resolving_total -
                                      resolving_mass_by_card[opponent_combo.first.value()] -
                                      resolving_mass_by_card[opponent_combo.second.value()] +
                                      exact_resolving_sequence_reach[value.combo];
    if (!std::isfinite(counterfactual_reach) || counterfactual_reach < 0.0) {
      return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    // The other player's reach gives the counterfactual mass at this River
    // root. The target player's own Flop/Turn action reach is continuation
    // probability from the parent Flop infoset and must therefore be present
    // when River roots are summed back into that infoset.
    source_counterfactual_reach[value.combo] =
        counterfactual_reach * opponent_postflop_action_sequence_reach[value.combo];
    observed_source[value.combo] = true;
  }
  if (std::count(observed_source.begin(), observed_source.end(), true) !=
      river_live_combos_per_board) {
    return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  for (const auto &permutation : lifts.value()) {
    const auto physical_turn = static_cast<std::uint8_t>((root.turn.value() / 4U) * 4U +
                                                         permutation[root.turn.value() % 4U]);
    TurnComponentAccumulator *turn_component = nullptr;
    if (boundary.value_mode == HuPreflopContinuationValueMode::ExactBestResponse) {
      turn_component = &best_response_turn_values[physical_turn];
    }
    for (const auto &value : source) {
      const auto physical_combo = transform_combo(value.combo, permutation);
      if (physical_combo >= masks.size() || (masks[physical_combo] & parent_flop_mask) != 0U) {
        return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      const auto reach = source_counterfactual_reach[value.combo] *
                         static_cast<double>(canonical_flop_entry->physical_outcome_count);
      const auto utility = reach * value.conditional_value_antes;
      compensated_add(reach, orbit_reach_sum[physical_combo],
                      orbit_reach_compensation[physical_combo]);
      compensated_add(utility, orbit_utility_sum[physical_combo],
                      orbit_utility_compensation[physical_combo]);
      if (turn_component != nullptr) {
        compensated_add(reach, turn_component->reach_sum[physical_combo],
                        turn_component->reach_compensation[physical_combo]);
        compensated_add(utility, turn_component->utility_sum[physical_combo],
                        turn_component->utility_compensation[physical_combo]);
      }
    }
  }
  boundary.best_response_turn_components.reserve(best_response_turn_values.size());
  for (const auto &[turn, component_values] : best_response_turn_values) {
    HuPreflopRiverBestResponseTurnComponent component;
    const auto physical_turn = CardId::from_index(turn);
    if (!physical_turn) {
      return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    component.turn = physical_turn.value();
    component.values.reserve(live_combos_per_flop);
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & parent_flop_mask) != 0U) {
        continue;
      }
      const auto reach =
          component_values.reach_sum[combo] + component_values.reach_compensation[combo];
      const auto utility =
          component_values.utility_sum[combo] + component_values.utility_compensation[combo];
      if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
          (reach == 0.0 && utility != 0.0)) {
        return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      component.values.push_back(
          {static_cast<ComboId>(combo), reach, reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
    }
    boundary.best_response_turn_components.push_back(std::move(component));
  }
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    if ((masks[combo] & parent_flop_mask) != 0U) {
      continue;
    }
    const auto reach = orbit_reach_sum[combo] + orbit_reach_compensation[combo];
    const auto utility = orbit_utility_sum[combo] + orbit_utility_compensation[combo];
    if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
        (reach == 0.0 && utility != 0.0)) {
      return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    boundary.values.push_back(
        {static_cast<ComboId>(combo), reach, reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
  }
  if (!valid_river_root_boundary_components(boundary)) {
    return Result<HuPreflopRiverRootBoundary, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  boundary.fingerprint = river_root_boundary_fingerprint(boundary);
  return Result<HuPreflopRiverRootBoundary, HuPreflopError>::success(std::move(boundary));
}

Result<HuPreflopRiverConditionedReach, HuPreflopError> condition_hu_preflop_ranges_on_river_root(
    const HuPreflopDecompositionPlan &decomposition, const HuPreflopRiverResolverRoot &root,
    const std::array<HuPreflopComboReach, 2> &postflop_action_sequence_reach) {
  const auto *entry = find_entry(decomposition, root.entry_node);
  if (entry == nullptr ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      root.resolving_player > 1U || root.fingerprint != river_resolver_root_fingerprint(root) ||
      std::popcount(root.state.board_mask) != 5) {
    return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  HuPreflopRiverConditionedReach result;
  result.resolver_root_fingerprint = root.fingerprint;
  const auto masks = combo_masks();
  for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      const auto action_reach = postflop_action_sequence_reach[player][combo];
      if (!std::isfinite(action_reach) || action_reach < 0.0 || action_reach > 1.0) {
        return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      if ((masks[combo] & root.state.board_mask) != 0U) {
        continue;
      }
      const auto reach = entry->own_sequence_reach[player][combo] * action_reach;
      if (!std::isfinite(reach) || reach < 0.0) {
        return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      result.postflop_action_sequence_reach[player][combo] = action_reach;
      result.exact_sequence_reach[player][combo] = reach;
      result.live_positive_combo_count[player] += reach > 0.0 ? 1U : 0U;
    }
  }
  const auto combos = all_combos();
  result.compatible_joint_reach_mass =
      compatible_joint_mass(combos, masks, result.exact_sequence_reach[0],
                            result.exact_sequence_reach[1], root.state.board_mask);
  if (!std::isfinite(result.compatible_joint_reach_mass) ||
      result.compatible_joint_reach_mass < 0.0) {
    return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
        HuPreflopError::NumericalFailure);
  }
  return Result<HuPreflopRiverConditionedReach, HuPreflopError>::success(std::move(result));
}

Result<HuPreflopRiverConditionedReach, HuPreflopError> derive_hu_preflop_river_conditioned_reach(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPostflopActionProbabilityProvider &probability_provider) {
  if (!probability_provider || tree.fingerprint != decomposition.tree_fingerprint ||
      decomposition.fingerprint != fingerprint_hu_preflop_decomposition_plan(decomposition) ||
      root.entry_node >= tree.nodes.size() ||
      tree.nodes[root.entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      root.fingerprint != river_resolver_root_fingerprint(root) ||
      root.state.street != Street::River || root.state.status != HandStatus::InProgress) {
    return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  ActionConfig action_config;
  action_config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                        tree.config.postflop_sizes.end());
  action_config.raise_depth = maximum_core_raise_depth;
  action_config.minimum_bet = tree.config.postflop_minimum_bet;
  action_config.all_in_mode = AllInMode::Add;
  action_config.all_in_threshold = PotPercentage::from_basis_points(100'000).value();

  auto state = tree.nodes[root.entry_node].state;
  state.board_mask = flop_mask(root.flop);
  std::vector<Action> action_prefix;
  action_prefix.reserve(root.action_history.size());
  std::array<HuPreflopComboReach, 2> action_sequence_reach;
  action_sequence_reach[0].fill(1.0);
  action_sequence_reach[1].fill(1.0);
  const auto masks = combo_masks();

  const auto advance_with_board = [&]() -> bool {
    const auto advanced = advance_street(state);
    if (!advanced) {
      return false;
    }
    state = advanced.value();
    if (state.street == Street::Turn) {
      state.board_mask |= root.turn.mask();
    } else if (state.street == Street::River) {
      state.board_mask |= root.river.mask();
    }
    return true;
  };

  for (const auto &action : root.action_history) {
    while (state.status == HandStatus::StreetComplete) {
      if (!advance_with_board() || state.street == Street::River) {
        return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
    }
    if (state.status != HandStatus::InProgress || state.player_to_act > 1U) {
      return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto legal = legal_actions(state, action_config);
    if (!legal || std::ranges::find(legal.value(), action) == legal.value().end()) {
      return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto actor = state.player_to_act;
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & state.board_mask) != 0U) {
        continue;
      }
      const auto probability = probability_provider(state, std::span<const Action>{action_prefix},
                                                    action, static_cast<ComboId>(combo));
      if (!probability || !std::isfinite(probability.value()) || probability.value() < 0.0 ||
          probability.value() > 1.0) {
        return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
            probability ? HuPreflopError::NumericalFailure : probability.error());
      }
      action_sequence_reach[actor][combo] *= probability.value();
      if (!std::isfinite(action_sequence_reach[actor][combo])) {
        return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
    }
    const auto next = apply_action(state, action, action_config);
    if (!next) {
      return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
          HuPreflopError::GameFailure);
    }
    state = next.value();
    action_prefix.push_back(action);
  }
  while (state.status == HandStatus::StreetComplete && state.street < Street::River) {
    if (!advance_with_board()) {
      return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
          HuPreflopError::GameFailure);
    }
  }
  if (state != root.state || action_prefix != root.action_history) {
    return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  return condition_hu_preflop_ranges_on_river_root(decomposition, root, action_sequence_reach);
}

Result<HuPreflopRiverTaskAccumulator, HuPreflopError> make_hu_preflop_river_task_accumulator(
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const std::uint64_t task_span_index, const std::uint64_t blueprint_iterations,
    const HuPreflopContinuationValueMode value_mode) {
  if (!valid_river_batch_plan_for_catalog(batch_plan, catalog) ||
      task_span_index >= catalog.task_spans.size() || blueprint_iterations == 0U ||
      !valid_continuation_value_mode(value_mode)) {
    return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto &span = catalog.task_spans[task_span_index];
  if (span.first_board_index >= catalog.canonical_boards.size()) {
    return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  HuPreflopRiverTaskAccumulator accumulator;
  accumulator.tree_fingerprint = catalog.tree_fingerprint;
  accumulator.blueprint_fingerprint = batch_plan.blueprint_fingerprint;
  accumulator.root_catalog_fingerprint = catalog.fingerprint;
  accumulator.batch_plan_fingerprint = batch_plan.fingerprint;
  accumulator.value_mode = value_mode;
  accumulator.blueprint_iterations = blueprint_iterations;
  accumulator.task_span_index = task_span_index;
  accumulator.first_resolver_root = span.first_resolver_root;
  accumulator.resolver_root_count = span.resolver_root_count;
  accumulator.next_resolver_root = span.first_resolver_root;
  accumulator.entry_node = span.entry_node;
  accumulator.flop = catalog.canonical_boards[span.first_board_index].flop;
  accumulator.contribution_chain_fingerprint =
      "fnv1a64:" + hex64(fnv1a("gtosd.hu_preflop_river_task_contributions.v5|" +
                               std::to_string(task_span_index) + "|" +
                               std::to_string(static_cast<unsigned>(value_mode))));
  accumulator.fingerprint = river_task_accumulator_fingerprint(accumulator);
  return valid_accumulator_payload(accumulator)
             ? Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::success(
                   std::move(accumulator))
             : Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<bool, HuPreflopError>
accumulate_hu_preflop_river_root_boundary(HuPreflopRiverTaskAccumulator &accumulator,
                                          const HuPreflopRiverRootBoundary &boundary) {
  if (!valid_accumulator_payload(accumulator) ||
      (!accumulator.fingerprint.empty() &&
       accumulator.fingerprint != river_task_accumulator_fingerprint(accumulator)) ||
      accumulator.complete || boundary.major != HuPreflopRiverRootBoundary::format_major ||
      boundary.minor != HuPreflopRiverRootBoundary::format_minor ||
      boundary.fingerprint != river_root_boundary_fingerprint(boundary) ||
      boundary.value_mode != accumulator.value_mode ||
      boundary.tree_fingerprint != accumulator.tree_fingerprint ||
      boundary.blueprint_fingerprint != accumulator.blueprint_fingerprint ||
      boundary.batch_plan_fingerprint != accumulator.batch_plan_fingerprint ||
      boundary.continuation_fingerprint.empty() ||
      (!accumulator.continuation_checkpoint_fingerprint.empty() &&
       boundary.continuation_fingerprint != accumulator.continuation_checkpoint_fingerprint) ||
      boundary.blueprint_iterations != accumulator.blueprint_iterations ||
      boundary.task_span_index != accumulator.task_span_index ||
      boundary.resolver_root_ordinal != accumulator.next_resolver_root ||
      boundary.entry_node != accumulator.entry_node || boundary.flop != accumulator.flop ||
      boundary.resolving_player > 1U || boundary.opponent != 1U - boundary.resolving_player ||
      boundary.resolving_player !=
          (boundary.resolver_root_ordinal - accumulator.first_resolver_root) % 2U ||
      boundary.physical_public_outcome_count == 0U ||
      !valid_river_root_boundary_components(boundary)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }

  std::unique_ptr<HuPreflopRiverTaskAccumulator> candidate;
  try {
    candidate = std::make_unique<HuPreflopRiverTaskAccumulator>(accumulator);
  } catch (const std::bad_alloc &) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
  }
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(boundary.flop);
  std::array<bool, 630U> observed{};
  for (const auto &value : boundary.values) {
    if (value.opponent_combo >= masks.size() || observed[value.opponent_combo] ||
        (masks[value.opponent_combo] & board_mask) != 0U ||
        !std::isfinite(value.counterfactual_reach) || value.counterfactual_reach < 0.0 ||
        !std::isfinite(value.blueprint_counterfactual_value_antes) ||
        value.positive_reach != (value.counterfactual_reach > 0.0) ||
        (!value.positive_reach && value.blueprint_counterfactual_value_antes != 0.0)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
    // The boundary builder has already lifted the representative River board
    // to every physical board in its suit orbit and remapped private combos.
    const auto weighted_reach = value.counterfactual_reach;
    const auto weighted_utility = weighted_reach * value.blueprint_counterfactual_value_antes;
    if (!std::isfinite(weighted_reach) || !std::isfinite(weighted_utility)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
    auto &target = candidate->values[boundary.resolving_player][value.opponent_combo];
    compensated_add(weighted_reach, target.weighted_reach_sum, target.weighted_reach_compensation);
    compensated_add(weighted_utility, target.weighted_utility_sum_antes,
                    target.weighted_utility_compensation_antes);
    observed[value.opponent_combo] = true;
  }
  if (std::count(observed.begin(), observed.end(), true) != live_combos_per_flop) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  if (candidate->continuation_checkpoint_fingerprint.empty()) {
    candidate->continuation_checkpoint_fingerprint = boundary.continuation_fingerprint;
  }
  ++candidate->accumulated_roots_by_resolver[boundary.resolving_player];
  ++candidate->next_resolver_root;
  candidate->complete = candidate->next_resolver_root - candidate->first_resolver_root ==
                        candidate->resolver_root_count;
  candidate->contribution_chain_fingerprint =
      "fnv1a64:" +
      hex64(fnv1a(candidate->contribution_chain_fingerprint + "|" + boundary.fingerprint));
  candidate->fingerprint.clear();
  if (!valid_accumulator_payload(*candidate)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  accumulator = std::move(*candidate);
  return Result<bool, HuPreflopError>::success(true);
}

Result<bool, HuPreflopError>
seal_hu_preflop_river_task_accumulator(HuPreflopRiverTaskAccumulator &accumulator) {
  if (!valid_accumulator_payload(accumulator)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  accumulator.fingerprint = river_task_accumulator_fingerprint(accumulator);
  return Result<bool, HuPreflopError>::success(true);
}

Result<bool, HuPreflopError>
validate_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator) {
  return valid_accumulator_payload(accumulator) && !accumulator.fingerprint.empty() &&
                 accumulator.fingerprint == river_task_accumulator_fingerprint(accumulator)
             ? Result<bool, HuPreflopError>::success(true)
             : Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

Result<bool, HuPreflopError>
validate_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate) {
  if (aggregate.major != HuPreflopRiverTaskAggregate::format_major ||
      aggregate.minor != HuPreflopRiverTaskAggregate::format_minor ||
      aggregate.tree_fingerprint.empty() || aggregate.blueprint_fingerprint.empty() ||
      aggregate.accumulator_fingerprint.empty() ||
      aggregate.continuation_checkpoint_fingerprint.empty() ||
      aggregate.blueprint_iterations == 0U ||
      !valid_continuation_value_mode(aggregate.value_mode) || !valid_flop(aggregate.flop) ||
      aggregate.fingerprint.empty() ||
      aggregate.fingerprint != river_task_aggregate_fingerprint(aggregate)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(aggregate.flop);
  for (const auto &player : aggregate.resolver_values) {
    if (player.size() != live_combos_per_flop) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    std::array<bool, 630U> observed{};
    for (const auto &value : player) {
      const auto combo = static_cast<std::size_t>(value.opponent_combo);
      if (combo >= masks.size() || observed[combo] || (masks[combo] & board_mask) != 0U ||
          !std::isfinite(value.weighted_counterfactual_reach) ||
          value.weighted_counterfactual_reach < 0.0 ||
          !std::isfinite(value.weighted_counterfactual_utility_antes) ||
          !std::isfinite(value.conditional_value_antes) ||
          value.positive_reach != (value.weighted_counterfactual_reach > 0.0)) {
        return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      if (value.weighted_counterfactual_reach == 0.0) {
        if (value.weighted_counterfactual_utility_antes != 0.0 ||
            value.conditional_value_antes != 0.0) {
          return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
        }
      } else {
        const auto expected =
            value.weighted_counterfactual_utility_antes / value.weighted_counterfactual_reach;
        const auto tolerance =
            1.0e-12 * std::max({1.0, std::abs(expected), std::abs(value.conditional_value_antes)});
        if (std::abs(value.conditional_value_antes - expected) > tolerance) {
          return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
        }
      }
      observed[combo] = true;
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<HuPreflopRiverTaskAggregate, HuPreflopError>
finalize_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator) {
  if (!valid_accumulator_payload(accumulator) || !accumulator.complete ||
      accumulator.fingerprint.empty() ||
      accumulator.fingerprint != river_task_accumulator_fingerprint(accumulator) ||
      accumulator.accumulated_roots_by_resolver[0] != accumulator.resolver_root_count / 2U ||
      accumulator.accumulated_roots_by_resolver[1] != accumulator.resolver_root_count / 2U) {
    return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  HuPreflopRiverTaskAggregate result;
  result.tree_fingerprint = accumulator.tree_fingerprint;
  result.blueprint_fingerprint = accumulator.blueprint_fingerprint;
  result.accumulator_fingerprint = accumulator.fingerprint;
  result.continuation_checkpoint_fingerprint = accumulator.continuation_checkpoint_fingerprint;
  result.value_mode = accumulator.value_mode;
  result.blueprint_iterations = accumulator.blueprint_iterations;
  result.task_span_index = accumulator.task_span_index;
  result.entry_node = accumulator.entry_node;
  result.flop = accumulator.flop;
  const auto masks = combo_masks();
  const auto board_mask = flop_mask(accumulator.flop);
  for (std::uint8_t resolver = 0U; resolver < 2U; ++resolver) {
    auto &output = result.resolver_values[resolver];
    output.reserve(live_combos_per_flop);
    for (std::size_t combo = 0U; combo < masks.size(); ++combo) {
      if ((masks[combo] & board_mask) != 0U) {
        continue;
      }
      const auto &source = accumulator.values[resolver][combo];
      const auto reach = source.weighted_reach_sum + source.weighted_reach_compensation;
      const auto utility =
          source.weighted_utility_sum_antes + source.weighted_utility_compensation_antes;
      if (!std::isfinite(reach) || reach < 0.0 || !std::isfinite(utility) ||
          (reach == 0.0 && utility != 0.0)) {
        return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      output.push_back({static_cast<ComboId>(combo), reach, utility,
                        reach > 0.0 ? utility / reach : 0.0, reach > 0.0});
    }
  }
  result.fingerprint = river_task_aggregate_fingerprint(result);
  return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::success(std::move(result));
}

Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
make_hu_preflop_river_scheduler_checkpoint(const HuPreflopRiverBatchPlan &plan,
                                           std::string upper_street_accumulator_fingerprint) {
  if (!valid_river_batch_plan(plan) || upper_street_accumulator_fingerprint.empty()) {
    return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  HuPreflopRiverSchedulerCheckpoint result;
  result.tree_fingerprint = plan.tree_fingerprint;
  result.blueprint_fingerprint = plan.blueprint_fingerprint;
  result.batch_plan_fingerprint = plan.fingerprint;
  result.upper_street_accumulator_fingerprint = std::move(upper_street_accumulator_fingerprint);
  result.fingerprint = river_scheduler_checkpoint_fingerprint(result);
  return valid_river_scheduler_checkpoint(plan, result)
             ? Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::success(std::move(result))
             : Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
advance_hu_preflop_river_scheduler_checkpoint(const HuPreflopRiverBatchPlan &plan,
                                              const HuPreflopRiverSchedulerCheckpoint &checkpoint,
                                              const HuPreflopRiverBatch &completed_batch,
                                              std::string upper_street_accumulator_fingerprint) {
  const auto expected_batch = hu_preflop_river_batch_at(plan, checkpoint.completed_batch_count);
  if (!valid_river_scheduler_checkpoint(plan, checkpoint) || checkpoint.complete ||
      !expected_batch || completed_batch.fingerprint != expected_batch.value().fingerprint ||
      completed_batch.batch_plan_fingerprint != plan.fingerprint ||
      upper_street_accumulator_fingerprint.empty() ||
      upper_street_accumulator_fingerprint == checkpoint.upper_street_accumulator_fingerprint) {
    return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  auto result = checkpoint;
  ++result.completed_batch_count;
  result.completed_resolver_root_count += completed_batch.resolver_root_count;
  result.upper_street_accumulator_fingerprint = std::move(upper_street_accumulator_fingerprint);
  result.complete = result.completed_batch_count == plan.batch_count;
  result.fingerprint = river_scheduler_checkpoint_fingerprint(result);
  return valid_river_scheduler_checkpoint(plan, result)
             ? Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::success(std::move(result))
             : Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

} // namespace gtosd
