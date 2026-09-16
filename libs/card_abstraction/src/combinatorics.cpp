#include "gtosd/card_abstraction/combinatorics.hpp"

#include <cstddef>
#include <cstdint>

namespace gtosd::card_abstraction {

std::uint64_t subset_index(const std::span<const std::uint8_t> sorted_values) noexcept {
  if (sorted_values.empty() || sorted_values.size() > maximum_subset_size) {
    return invalid_subset_index;
  }
  std::uint64_t index = 0U;
  for (std::size_t position = 0U; position < sorted_values.size(); ++position) {
    const auto value = sorted_values[position];
    if (value >= deck_cards || (position > 0U && value <= sorted_values[position - 1U])) {
      return invalid_subset_index;
    }
    index += binomial(value, static_cast<std::uint32_t>(position + 1U));
  }
  return index;
}

bool subset_from_index(std::uint64_t index, const std::span<std::uint8_t> output) noexcept {
  const auto size = static_cast<std::uint32_t>(output.size());
  if (size == 0U || size > maximum_subset_size || index >= binomial(deck_cards, size)) {
    return false;
  }
  std::uint32_t upper = deck_cards;
  for (std::uint32_t position = size; position > 0U; --position) {
    // Largest value v < upper with C(v, position) <= index.
    std::uint32_t value = upper - 1U;
    while (binomial(value, position) > index) {
      --value;
    }
    output[position - 1U] = static_cast<std::uint8_t>(value);
    index -= binomial(value, position);
    upper = value;
  }
  return true;
}

Combo combo_from_index(std::uint16_t index) noexcept {
  std::uint8_t first = 0U;
  while (first < deck_cards - 1U) {
    const auto count = static_cast<std::uint16_t>(deck_cards - 1U - first);
    if (index < count) {
      break;
    }
    index = static_cast<std::uint16_t>(index - count);
    ++first;
  }
  const auto second = static_cast<std::uint8_t>(first + 1U + index);
  return Combo{CardId::from_index(first).value(),
               CardId::from_index(second < deck_cards ? second : static_cast<std::uint8_t>(35U))
                   .value()};
}

} // namespace gtosd::card_abstraction
