#include "gtosd/isomorphism/isomorphism.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>
#include <utility>

namespace gtosd {
namespace {

constexpr std::uint64_t valid_deck_mask = (std::uint64_t{1} << 36U) - 1U;

bool valid_permutation(const SuitPermutation &permutation) {
  std::array<bool, 4> seen{};
  for (const auto suit : permutation.forward) {
    const auto index = static_cast<std::size_t>(suit);
    if (index >= seen.size() || seen[index]) {
      return false;
    }
    seen[index] = true;
  }
  return true;
}

std::array<SuitPermutation, 24> make_permutations() {
  std::array<SuitPermutation, 24> result{};
  std::array<std::uint8_t, 4> values{0, 1, 2, 3};
  std::size_t output = 0;
  do {
    SuitPermutation permutation;
    for (std::size_t index = 0; index < values.size(); ++index) {
      permutation.forward[index] = static_cast<Suit>(values[index]);
    }
    result[output++] = permutation;
  } while (std::next_permutation(values.begin(), values.end()));
  return result;
}

std::vector<CardId> cards_from_mask(const std::uint64_t mask) {
  std::vector<CardId> cards;
  cards.reserve(static_cast<std::size_t>(std::popcount(mask)));
  for (std::uint8_t index = 0; index < 36U; ++index) {
    if ((mask & (std::uint64_t{1} << index)) != 0U) {
      cards.push_back(CardId::from_index(index).value());
    }
  }
  return cards;
}

std::pair<std::uint8_t, std::uint8_t> normalized_combo_values(const Combo combo) {
  return std::minmax(combo.first.value(), combo.second.value());
}

bool valid_combo(const Combo combo) { return combo.first != combo.second; }

bool combo_less(const WeightedCombo &left, const WeightedCombo &right) {
  return std::tuple{normalized_combo_values(left.combo), left.weight.basis_points()} <
         std::tuple{normalized_combo_values(right.combo), right.weight.basis_points()};
}

bool nodelock_less(const NodelockEntry &left, const NodelockEntry &right) {
  const auto left_hand = normalized_combo_values(left.private_hand);
  const auto right_hand = normalized_combo_values(right.private_hand);
  const auto left_identity =
      std::tuple{left.board_mask, left.player, left.betting_history, left_hand};
  const auto right_identity =
      std::tuple{right.board_mask, right.player, right.betting_history, right_hand};
  if (left_identity != right_identity) {
    return left_identity < right_identity;
  }
  return std::lexicographical_compare(left.action_weights.begin(), left.action_weights.end(),
                                      right.action_weights.begin(), right.action_weights.end(),
                                      [](const RangeWeight first, const RangeWeight second) {
                                        return first.basis_points() < second.basis_points();
                                      });
}

Result<bool, IsomorphismError> validate_input(const CanonicalStateInput &input) {
  if ((input.public_state.board_mask & ~valid_deck_mask) != 0U) {
    return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidBoard);
  }

  std::uint64_t physical_mask = input.public_state.board_mask;
  std::uint64_t range_blocked_mask = input.public_state.board_mask;
  const auto add_physical_card = [&physical_mask](const CardId card) {
    if ((physical_mask & card.mask()) != 0U) {
      return false;
    }
    physical_mask |= card.mask();
    return true;
  };
  for (const auto hand : input.private_deal) {
    if (!valid_combo(hand) || !add_physical_card(hand.first) || !add_physical_card(hand.second)) {
      return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidPhysicalCards);
    }
  }
  for (const auto card : input.dead_cards) {
    if (!add_physical_card(card)) {
      return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidPhysicalCards);
    }
    range_blocked_mask |= card.mask();
  }
  for (const auto card : input.future_cards) {
    if (!add_physical_card(card)) {
      return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidPhysicalCards);
    }
    range_blocked_mask |= card.mask();
  }

  for (const auto &range : input.ranges) {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> combos;
    combos.reserve(range.size());
    for (const auto &entry : range) {
      if (!valid_combo(entry.combo) ||
          ((entry.combo.first.mask() | entry.combo.second.mask()) & range_blocked_mask) != 0U) {
        return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidRange);
      }
      combos.push_back(normalized_combo_values(entry.combo));
    }
    std::ranges::sort(combos);
    if (std::adjacent_find(combos.begin(), combos.end()) != combos.end()) {
      return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidRange);
    }
  }

  std::vector<
      std::tuple<std::uint64_t, std::uint8_t, std::string, std::pair<std::uint8_t, std::uint8_t>>>
      nodelock_identities;
  nodelock_identities.reserve(input.nodelocks.size());
  for (const auto &nodelock : input.nodelocks) {
    if ((nodelock.board_mask & ~valid_deck_mask) != 0U || !valid_combo(nodelock.private_hand) ||
        ((nodelock.private_hand.first.mask() | nodelock.private_hand.second.mask()) &
         nodelock.board_mask) != 0U ||
        nodelock.player >= maximum_players || nodelock.action_weights.empty()) {
      return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidNodelock);
    }
    const auto total_weight =
        std::accumulate(nodelock.action_weights.begin(), nodelock.action_weights.end(),
                        std::uint32_t{0}, [](const std::uint32_t total, const RangeWeight value) {
                          return total + value.basis_points();
                        });
    if (total_weight != 10'000U) {
      return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidNodelock);
    }
    nodelock_identities.emplace_back(nodelock.board_mask, nodelock.player, nodelock.betting_history,
                                     normalized_combo_values(nodelock.private_hand));
  }
  std::ranges::sort(nodelock_identities);
  if (std::adjacent_find(nodelock_identities.begin(), nodelock_identities.end()) !=
      nodelock_identities.end()) {
    return Result<bool, IsomorphismError>::failure(IsomorphismError::InvalidNodelock);
  }
  return Result<bool, IsomorphismError>::success(true);
}

void append_text(std::ostringstream &output, const std::string_view text) {
  output << text.size() << ':' << text;
}

void append_combo(std::ostringstream &output, const Combo combo) {
  const auto [first, second] = normalized_combo_values(combo);
  output << static_cast<unsigned>(first) << ',' << static_cast<unsigned>(second);
}

std::uint64_t fnv1a(const std::string_view text) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const auto character : text) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

CardId transform_card_unchecked(const CardId card, const SuitPermutation &permutation) {
  return CardId::from_parts(card.rank(),
                            permutation.forward[static_cast<std::size_t>(card.suit())]);
}

std::uint64_t transform_mask_unchecked(const std::uint64_t mask,
                                       const SuitPermutation &permutation) {
  std::uint64_t transformed = 0;
  for (const auto card : cards_from_mask(mask)) {
    transformed |= transform_card_unchecked(card, permutation).mask();
  }
  return transformed;
}

Combo transform_combo_unchecked(const Combo combo, const SuitPermutation &permutation) {
  return {transform_card_unchecked(combo.first, permutation),
          transform_card_unchecked(combo.second, permutation)};
}

WeightedRange transform_range_unchecked(const WeightedRange &range,
                                        const SuitPermutation &permutation) {
  WeightedRange transformed;
  transformed.reserve(range.size());
  for (const auto &entry : range) {
    transformed.push_back(
        WeightedCombo{transform_combo_unchecked(entry.combo, permutation), entry.weight});
  }
  std::ranges::sort(transformed, combo_less);
  return transformed;
}

NodelockEntry transform_nodelock_unchecked(const NodelockEntry &nodelock,
                                           const SuitPermutation &permutation) {
  auto transformed = nodelock;
  transformed.board_mask = transform_mask_unchecked(nodelock.board_mask, permutation);
  transformed.private_hand = transform_combo_unchecked(nodelock.private_hand, permutation);
  return transformed;
}

CanonicalStateInput transform_state_unchecked(const CanonicalStateInput &input,
                                              const SuitPermutation &permutation) {
  auto transformed = input;
  transformed.public_state.board_mask =
      transform_mask_unchecked(input.public_state.board_mask, permutation);
  for (std::size_t player = 0; player < input.ranges.size(); ++player) {
    transformed.ranges[player] = transform_range_unchecked(input.ranges[player], permutation);
  }
  for (std::size_t index = 0; index < input.private_deal.size(); ++index) {
    transformed.private_deal[index] =
        transform_combo_unchecked(input.private_deal[index], permutation);
  }
  for (std::size_t index = 0; index < input.dead_cards.size(); ++index) {
    transformed.dead_cards[index] = transform_card_unchecked(input.dead_cards[index], permutation);
  }
  for (std::size_t index = 0; index < input.future_cards.size(); ++index) {
    transformed.future_cards[index] =
        transform_card_unchecked(input.future_cards[index], permutation);
  }
  for (std::size_t index = 0; index < input.nodelocks.size(); ++index) {
    transformed.nodelocks[index] =
        transform_nodelock_unchecked(input.nodelocks[index], permutation);
  }
  std::ranges::sort(transformed.dead_cards);
  std::ranges::sort(transformed.future_cards);
  std::ranges::sort(transformed.nodelocks, nodelock_less);
  return transformed;
}

} // namespace

const std::array<SuitPermutation, 24> &all_suit_permutations() noexcept {
  static const auto permutations = make_permutations();
  return permutations;
}

Result<SuitPermutation, IsomorphismError> inverse_permutation(const SuitPermutation &permutation) {
  if (!valid_permutation(permutation)) {
    return Result<SuitPermutation, IsomorphismError>::failure(IsomorphismError::InvalidPermutation);
  }
  SuitPermutation inverse;
  for (std::size_t source = 0; source < permutation.forward.size(); ++source) {
    inverse.forward[static_cast<std::size_t>(permutation.forward[source])] =
        static_cast<Suit>(source);
  }
  return Result<SuitPermutation, IsomorphismError>::success(inverse);
}

Result<CardId, IsomorphismError> transform_card(const CardId card,
                                                const SuitPermutation &permutation) {
  if (!valid_permutation(permutation)) {
    return Result<CardId, IsomorphismError>::failure(IsomorphismError::InvalidPermutation);
  }
  return Result<CardId, IsomorphismError>::success(transform_card_unchecked(card, permutation));
}

Result<std::uint64_t, IsomorphismError> transform_card_mask(const std::uint64_t mask,
                                                            const SuitPermutation &permutation) {
  if (!valid_permutation(permutation)) {
    return Result<std::uint64_t, IsomorphismError>::failure(IsomorphismError::InvalidPermutation);
  }
  if ((mask & ~valid_deck_mask) != 0U) {
    return Result<std::uint64_t, IsomorphismError>::failure(IsomorphismError::InvalidBoard);
  }
  return Result<std::uint64_t, IsomorphismError>::success(
      transform_mask_unchecked(mask, permutation));
}

Result<Combo, IsomorphismError> transform_combo(const Combo combo,
                                                const SuitPermutation &permutation) {
  if (!valid_combo(combo)) {
    return Result<Combo, IsomorphismError>::failure(IsomorphismError::InvalidPhysicalCards);
  }
  if (!valid_permutation(permutation)) {
    return Result<Combo, IsomorphismError>::failure(IsomorphismError::InvalidPermutation);
  }
  return Result<Combo, IsomorphismError>::success(transform_combo_unchecked(combo, permutation));
}

Result<WeightedRange, IsomorphismError> transform_range(const WeightedRange &range,
                                                        const SuitPermutation &permutation) {
  std::vector<std::pair<std::uint8_t, std::uint8_t>> combos;
  combos.reserve(range.size());
  for (const auto &entry : range) {
    if (!valid_combo(entry.combo)) {
      return Result<WeightedRange, IsomorphismError>::failure(IsomorphismError::InvalidRange);
    }
    combos.push_back(normalized_combo_values(entry.combo));
  }
  std::ranges::sort(combos);
  if (std::adjacent_find(combos.begin(), combos.end()) != combos.end()) {
    return Result<WeightedRange, IsomorphismError>::failure(IsomorphismError::InvalidRange);
  }
  if (!valid_permutation(permutation)) {
    return Result<WeightedRange, IsomorphismError>::failure(IsomorphismError::InvalidPermutation);
  }
  return Result<WeightedRange, IsomorphismError>::success(
      transform_range_unchecked(range, permutation));
}

Result<NodelockEntry, IsomorphismError> transform_nodelock(const NodelockEntry &nodelock,
                                                           const SuitPermutation &permutation) {
  if ((nodelock.board_mask & ~valid_deck_mask) != 0U || !valid_combo(nodelock.private_hand) ||
      ((nodelock.private_hand.first.mask() | nodelock.private_hand.second.mask()) &
       nodelock.board_mask) != 0U ||
      nodelock.player >= maximum_players || nodelock.action_weights.empty()) {
    return Result<NodelockEntry, IsomorphismError>::failure(IsomorphismError::InvalidNodelock);
  }
  const auto total_weight =
      std::accumulate(nodelock.action_weights.begin(), nodelock.action_weights.end(),
                      std::uint32_t{0}, [](const std::uint32_t total, const RangeWeight value) {
                        return total + value.basis_points();
                      });
  if (total_weight != 10'000U) {
    return Result<NodelockEntry, IsomorphismError>::failure(IsomorphismError::InvalidNodelock);
  }
  if (!valid_permutation(permutation)) {
    return Result<NodelockEntry, IsomorphismError>::failure(IsomorphismError::InvalidPermutation);
  }
  return Result<NodelockEntry, IsomorphismError>::success(
      transform_nodelock_unchecked(nodelock, permutation));
}

Result<CanonicalStateInput, IsomorphismError> transform_state(const CanonicalStateInput &input,
                                                              const SuitPermutation &permutation) {
  const auto validated = validate_input(input);
  if (!validated) {
    return Result<CanonicalStateInput, IsomorphismError>::failure(validated.error());
  }
  if (!valid_permutation(permutation)) {
    return Result<CanonicalStateInput, IsomorphismError>::failure(
        IsomorphismError::InvalidPermutation);
  }
  return Result<CanonicalStateInput, IsomorphismError>::success(
      transform_state_unchecked(input, permutation));
}

std::string serialize_isomorphic_state(const CanonicalStateInput &input) {
  std::ostringstream output;
  output << "GTOSD_ISO_1|" << input.game_version << '|';
  append_text(output, serialize_public_state(input.public_state));
  output << '|';
  append_text(output, input.betting_history);
  for (const auto &range : input.ranges) {
    output << "|R" << range.size() << ':';
    for (const auto &entry : range) {
      append_combo(output, entry.combo);
      output << '@' << entry.weight.basis_points() << ';';
    }
  }
  output << "|P" << input.private_deal.size() << ':';
  for (const auto combo : input.private_deal) {
    append_combo(output, combo);
    output << ';';
  }
  output << "|D" << input.dead_cards.size() << ':';
  for (const auto card : input.dead_cards) {
    output << static_cast<unsigned>(card.value()) << ',';
  }
  output << "|F" << input.future_cards.size() << ':';
  for (const auto card : input.future_cards) {
    output << static_cast<unsigned>(card.value()) << ',';
  }
  output << "|N" << input.nodelocks.size() << ':';
  for (const auto &nodelock : input.nodelocks) {
    output << nodelock.board_mask << ',' << static_cast<unsigned>(nodelock.player) << ',';
    append_text(output, nodelock.betting_history);
    output << ',';
    append_combo(output, nodelock.private_hand);
    output << ',';
    for (const auto weight : nodelock.action_weights) {
      output << weight.basis_points() << '.';
    }
    output << ';';
  }
  return output.str();
}

Result<CanonicalState, IsomorphismError> canonicalize_state(const CanonicalStateInput &input) {
  const auto validated = validate_input(input);
  if (!validated) {
    return Result<CanonicalState, IsomorphismError>::failure(validated.error());
  }

  bool initialized = false;
  CanonicalState best;
  for (const auto &permutation : all_suit_permutations()) {
    auto transformed = transform_state_unchecked(input, permutation);
    auto key = serialize_isomorphic_state(transformed);
    if (!initialized || key < best.key) {
      initialized = true;
      best.key = std::move(key);
      best.state = std::move(transformed);
      best.physical_to_canonical = permutation;
      best.canonical_to_physical = inverse_permutation(permutation).value();
    }
  }
  return Result<CanonicalState, IsomorphismError>::success(std::move(best));
}

Result<std::vector<OrbitEntry>, IsomorphismError> audit_orbit(const CanonicalStateInput &input) {
  const auto canonical = canonicalize_state(input);
  if (!canonical) {
    return Result<std::vector<OrbitEntry>, IsomorphismError>::failure(canonical.error());
  }
  std::vector<OrbitEntry> entries;
  entries.reserve(all_suit_permutations().size());
  for (const auto &permutation : all_suit_permutations()) {
    const auto transformed = transform_state(input, permutation);
    if (!transformed) {
      return Result<std::vector<OrbitEntry>, IsomorphismError>::failure(transformed.error());
    }
    auto serialized = serialize_isomorphic_state(transformed.value());
    entries.push_back(OrbitEntry{permutation, serialized, serialized == canonical.value().key});
  }
  return Result<std::vector<OrbitEntry>, IsomorphismError>::success(std::move(entries));
}

Result<std::vector<CanonicalChanceEdge>, IsomorphismError>
aggregate_chance_outcomes(const CanonicalStateInput &parent,
                          const std::vector<CardId> &legal_cards) {
  if (legal_cards.empty()) {
    return Result<std::vector<CanonicalChanceEdge>, IsomorphismError>::failure(
        IsomorphismError::EmptyChanceOutcomes);
  }
  const auto validated = validate_input(parent);
  if (!validated) {
    return Result<std::vector<CanonicalChanceEdge>, IsomorphismError>::failure(validated.error());
  }

  std::uint64_t seen = 0;
  std::vector<CanonicalChanceEdge> result;
  for (const auto card : legal_cards) {
    if ((parent.public_state.board_mask & card.mask()) != 0U || (seen & card.mask()) != 0U) {
      return Result<std::vector<CanonicalChanceEdge>, IsomorphismError>::failure(
          IsomorphismError::IllegalChanceCard);
    }
    seen |= card.mask();
    auto child_input = parent;
    child_input.public_state.board_mask |= card.mask();
    for (auto &range : child_input.ranges) {
      std::erase_if(range, [card](const WeightedCombo &entry) {
        return entry.combo.first == card || entry.combo.second == card;
      });
    }
    const auto child = canonicalize_state(child_input);
    if (!child) {
      return Result<std::vector<CanonicalChanceEdge>, IsomorphismError>::failure(child.error());
    }
    const auto canonical_card = transform_card(card, child.value().physical_to_canonical).value();
    const auto found = std::ranges::find_if(
        result, [&child](const auto &edge) { return edge.canonical_key == child.value().key; });
    if (found == result.end()) {
      CanonicalChanceEdge edge;
      edge.canonical_key = child.value().key;
      edge.child = child.value();
      edge.representative_card = canonical_card;
      edge.physical_cards.push_back(card);
      edge.physical_outcome_count = 1;
      result.push_back(std::move(edge));
    } else {
      found->physical_cards.push_back(card);
      ++found->physical_outcome_count;
    }
  }
  for (auto &edge : result) {
    std::ranges::sort(edge.physical_cards);
    edge.total_legal_outcome_count = static_cast<std::uint32_t>(legal_cards.size());
  }
  std::ranges::sort(result, [](const auto &left, const auto &right) {
    return left.canonical_key < right.canonical_key;
  });
  return Result<std::vector<CanonicalChanceEdge>, IsomorphismError>::success(std::move(result));
}

Result<CanonicalState, IsomorphismError>
CanonicalKeyCache::canonicalize(const CanonicalStateInput &input) {
  ++stats_.queries;
  const auto validated = validate_input(input);
  if (!validated) {
    return Result<CanonicalState, IsomorphismError>::failure(validated.error());
  }
  const auto physical_key = serialize_isomorphic_state(input);
  const auto hash = fnv1a(physical_key);
  const auto bucket = entries_.find(hash);
  if (bucket != entries_.end()) {
    for (const auto &entry : bucket->second) {
      if (entry.physical_key == physical_key) {
        ++stats_.hits;
        return Result<CanonicalState, IsomorphismError>::success(entry.canonical);
      }
      ++stats_.hash_collisions;
    }
  }
  ++stats_.misses;
  auto canonical = canonicalize_state(input);
  if (!canonical) {
    return canonical;
  }
  entries_[hash].push_back(Entry{physical_key, canonical.value()});
  return canonical;
}

double CanonicalKeyCache::hit_rate() const noexcept {
  if (stats_.queries == 0U) {
    return 0.0;
  }
  return static_cast<double>(stats_.hits) / static_cast<double>(stats_.queries);
}

void CanonicalKeyCache::clear() {
  entries_.clear();
  stats_ = {};
}

std::string permutation_name(const SuitPermutation &permutation) {
  if (!valid_permutation(permutation)) {
    return "invalid";
  }
  constexpr std::array<char, 4> suit_names{'c', 'd', 'h', 's'};
  std::string result = "cdhs->";
  for (const auto suit : permutation.forward) {
    result.push_back(suit_names[static_cast<std::size_t>(suit)]);
  }
  return result;
}

const char *isomorphism_error_name(const IsomorphismError error) noexcept {
  switch (error) {
  case IsomorphismError::InvalidPermutation:
    return "invalid_permutation";
  case IsomorphismError::InvalidBoard:
    return "invalid_board";
  case IsomorphismError::InvalidPhysicalCards:
    return "invalid_physical_cards";
  case IsomorphismError::InvalidRange:
    return "invalid_range";
  case IsomorphismError::InvalidNodelock:
    return "invalid_nodelock";
  case IsomorphismError::EmptyChanceOutcomes:
    return "empty_chance_outcomes";
  case IsomorphismError::IllegalChanceCard:
    return "illegal_chance_card";
  }
  return "unknown_isomorphism_error";
}

} // namespace gtosd
