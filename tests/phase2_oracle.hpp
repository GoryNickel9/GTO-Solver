#pragma once

#include "gtosd/equity/evaluator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace phase2_oracle {
namespace detail {

constexpr std::uint8_t no_rank = 0xFFU;

inline std::uint8_t straight_high(const std::array<std::uint8_t, 9> &counts) {
  for (int high = 8; high >= 4; --high) {
    bool complete = true;
    for (int rank = high - 4; rank <= high; ++rank) {
      if (counts[static_cast<std::size_t>(rank)] == 0U) {
        complete = false;
        break;
      }
    }
    if (complete) {
      return static_cast<std::uint8_t>(high);
    }
  }
  if (counts[8] != 0U && counts[0] != 0U && counts[1] != 0U && counts[2] != 0U && counts[3] != 0U) {
    return 3U;
  }
  return no_rank;
}

inline std::array<std::uint8_t, 5> highest_distinct(const std::array<std::uint8_t, 9> &counts,
                                                    const std::uint8_t excluded_a = no_rank,
                                                    const std::uint8_t excluded_b = no_rank) {
  std::array<std::uint8_t, 5> result{};
  std::size_t output = 0;
  for (int rank = 8; rank >= 0 && output < result.size(); --rank) {
    if (static_cast<std::uint8_t>(rank) != excluded_a &&
        static_cast<std::uint8_t>(rank) != excluded_b &&
        counts[static_cast<std::size_t>(rank)] != 0U) {
      result[output++] = static_cast<std::uint8_t>(rank);
    }
  }
  return result;
}

inline std::uint8_t highest_with_count(const std::array<std::uint8_t, 9> &counts,
                                       const std::uint8_t minimum,
                                       const std::uint8_t excluded = no_rank) {
  for (int rank = 8; rank >= 0; --rank) {
    if (static_cast<std::uint8_t>(rank) != excluded &&
        counts[static_cast<std::size_t>(rank)] >= minimum) {
      return static_cast<std::uint8_t>(rank);
    }
  }
  return no_rank;
}

} // namespace detail

inline gtosd::HandValue evaluate(const std::span<const gtosd::CardId> cards) {
  std::array<std::uint8_t, 9> ranks{};
  std::array<std::uint8_t, 4> suit_counts{};
  std::array<std::array<std::uint8_t, 9>, 4> suited_ranks{};
  for (const auto card : cards) {
    const auto rank = static_cast<std::size_t>(card.rank());
    const auto suit = static_cast<std::size_t>(card.suit());
    ++ranks[rank];
    ++suit_counts[suit];
    suited_ranks[suit][rank] = 1U;
  }

  std::uint8_t flush_suit = detail::no_rank;
  for (std::uint8_t suit = 0; suit < 4U; ++suit) {
    if (suit_counts[suit] >= 5U) {
      flush_suit = suit;
      break;
    }
  }
  if (flush_suit != detail::no_rank) {
    const auto high = detail::straight_high(suited_ranks[flush_suit]);
    if (high != detail::no_rank) {
      return {gtosd::HandCategory::StraightFlush, {high, 0, 0, 0, 0}};
    }
  }

  const auto four = detail::highest_with_count(ranks, 4U);
  if (four != detail::no_rank) {
    return {gtosd::HandCategory::FourOfAKind,
            {four, detail::highest_distinct(ranks, four)[0], 0, 0, 0}};
  }
  if (flush_suit != detail::no_rank) {
    return {gtosd::HandCategory::Flush, detail::highest_distinct(suited_ranks[flush_suit])};
  }

  const auto three = detail::highest_with_count(ranks, 3U);
  if (three != detail::no_rank) {
    const auto pair = detail::highest_with_count(ranks, 2U, three);
    if (pair != detail::no_rank) {
      return {gtosd::HandCategory::FullHouse, {three, pair, 0, 0, 0}};
    }
  }

  const auto straight = detail::straight_high(ranks);
  if (straight != detail::no_rank) {
    return {gtosd::HandCategory::Straight, {straight, 0, 0, 0, 0}};
  }
  if (three != detail::no_rank) {
    const auto kickers = detail::highest_distinct(ranks, three);
    return {gtosd::HandCategory::ThreeOfAKind, {three, kickers[0], kickers[1], 0, 0}};
  }

  const auto high_pair = detail::highest_with_count(ranks, 2U);
  if (high_pair != detail::no_rank) {
    const auto low_pair = detail::highest_with_count(ranks, 2U, high_pair);
    if (low_pair != detail::no_rank) {
      return {gtosd::HandCategory::TwoPair,
              {high_pair, low_pair, detail::highest_distinct(ranks, high_pair, low_pair)[0], 0, 0}};
    }
    const auto kickers = detail::highest_distinct(ranks, high_pair);
    return {gtosd::HandCategory::Pair, {high_pair, kickers[0], kickers[1], kickers[2], 0}};
  }
  return {gtosd::HandCategory::HighCard, detail::highest_distinct(ranks)};
}

inline gtosd::CardId permute_suit(const gtosd::CardId card,
                                  const std::array<std::uint8_t, 4> &permutation) {
  return gtosd::CardId::from_parts(
      card.rank(), static_cast<gtosd::Suit>(permutation[static_cast<std::size_t>(card.suit())]));
}

} // namespace phase2_oracle
