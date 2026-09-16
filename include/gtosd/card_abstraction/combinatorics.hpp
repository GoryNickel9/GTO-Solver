#pragma once

#include "gtosd/core/cards.hpp"
#include "gtosd/core/ranges.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Exact combinatorics for the 36-card Short Deck.
//
// Two indexings coexist and are never mixed:
// - `subset_index` is the colex rank of a strictly increasing k-subset of card
//   indices (k in 1..7). It is the natural key for precomputed tables over
//   3-, 5- and 7-card sets.
// - `combo_index` reproduces the lexicographic pair order of
//   `gtosd::all_combos()`, so it is interchangeable with `ComboId`.
namespace gtosd::card_abstraction {

inline constexpr std::uint32_t deck_cards = 36U;
inline constexpr std::uint32_t maximum_subset_size = 7U;

namespace detail {

constexpr std::array<std::array<std::uint64_t, maximum_subset_size + 1U>, deck_cards + 1U>
make_binomial_table() noexcept {
  std::array<std::array<std::uint64_t, maximum_subset_size + 1U>, deck_cards + 1U> table{};
  for (std::uint32_t n = 0U; n <= deck_cards; ++n) {
    table[n][0] = 1U;
    for (std::uint32_t k = 1U; k <= maximum_subset_size; ++k) {
      table[n][k] = n == 0U ? 0U : table[n - 1U][k - 1U] + table[n - 1U][k];
    }
  }
  return table;
}

inline constexpr auto binomial_table = make_binomial_table();

} // namespace detail

// C(n, k) for n <= 36 and k <= 7; zero outside that domain.
[[nodiscard]] constexpr std::uint64_t binomial(const std::uint32_t n,
                                               const std::uint32_t k) noexcept {
  if (n > deck_cards || k > maximum_subset_size) {
    return 0U;
  }
  return detail::binomial_table[n][k];
}

// Colex rank of a strictly increasing subset of {0..35}. The caller guarantees
// ordering, distinctness and 1 <= size <= 7; violations return the sentinel
// below.
inline constexpr std::uint64_t invalid_subset_index = ~std::uint64_t{0};
[[nodiscard]] std::uint64_t subset_index(std::span<const std::uint8_t> sorted_values) noexcept;

// Inverse of subset_index for k = output.size(); writes the subset in
// increasing order. Returns false when the index is out of range.
[[nodiscard]] bool subset_from_index(std::uint64_t index, std::span<std::uint8_t> output) noexcept;

// Pair index in the order of gtosd::all_combos(): (0,1),(0,2),...,(34,35).
[[nodiscard]] constexpr std::uint16_t combo_index(const CardId first,
                                                  const CardId second) noexcept {
  const auto a = first < second ? first.value() : second.value();
  const auto b = first < second ? second.value() : first.value();
  const auto preceding = static_cast<std::uint32_t>(a) * 35U -
                         (static_cast<std::uint32_t>(a) * (static_cast<std::uint32_t>(a) + 1U)) /
                             2U +
                         static_cast<std::uint32_t>(a);
  return static_cast<std::uint16_t>(preceding + static_cast<std::uint32_t>(b - a - 1U));
}

[[nodiscard]] Combo combo_from_index(std::uint16_t index) noexcept;

} // namespace gtosd::card_abstraction
