#include "gtosd/core/ranges.hpp"

#include <algorithm>

namespace gtosd {

std::array<Combo, 630> all_combos() {
  const auto deck = short_deck();
  std::array<Combo, 630> result{};
  std::size_t output = 0;
  for (std::size_t first = 0; first < deck.size(); ++first) {
    for (std::size_t second = first + 1; second < deck.size(); ++second) {
      result[output++] = {deck[first], deck[second]};
    }
  }
  return result;
}

HandClassId hand_class(const Combo combo) {
  const auto first_rank = static_cast<std::uint8_t>(combo.first.rank());
  const auto second_rank = static_cast<std::uint8_t>(combo.second.rank());
  const auto high = std::max(first_rank, second_rank);
  const auto low = std::min(first_rank, second_rank);
  if (high == low) {
    return static_cast<HandClassId>(8U - high);
  }
  std::uint8_t preceding = 0;
  for (std::uint8_t candidate_high = 8; candidate_high > high; --candidate_high) {
    preceding += candidate_high;
  }
  preceding += static_cast<std::uint8_t>(high - low - 1U);
  const auto suited_offset = combo.first.suit() == combo.second.suit() ? 9U : 45U;
  return static_cast<HandClassId>(suited_offset + preceding);
}

std::uint8_t class_mass(const HandClassId hand_class_id) {
  if (hand_class_id < 9U) {
    return 6U;
  }
  if (hand_class_id < 45U) {
    return 4U;
  }
  return 12U;
}

std::string class_name(const HandClassId hand_class_id) {
  constexpr char ranks[] = "AKQJT9876";
  if (hand_class_id < 9U) {
    return std::string(2, ranks[hand_class_id]);
  }
  const bool suited = hand_class_id < 45U;
  auto remaining = static_cast<std::uint8_t>(hand_class_id - (suited ? 9U : 45U));
  for (std::uint8_t high = 0; high < 8U; ++high) {
    const auto count = static_cast<std::uint8_t>(8U - high);
    if (remaining < count) {
      const auto low = static_cast<std::uint8_t>(high + 1U + remaining);
      return {ranks[high], ranks[low], suited ? 's' : 'o'};
    }
    remaining = static_cast<std::uint8_t>(remaining - count);
  }
  return {};
}

} // namespace gtosd
