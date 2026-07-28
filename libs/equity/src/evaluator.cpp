#include "gtosd/equity/evaluator.hpp"

#include <algorithm>

namespace gtosd {
namespace {

constexpr std::uint8_t no_rank = 0xFFU;

template <std::size_t Size> bool has_duplicate(const std::array<CardId, Size> &cards) {
  std::uint64_t mask = 0;
  for (const auto card : cards) {
    if ((mask & card.mask()) != 0U) {
      return true;
    }
    mask |= card.mask();
  }
  return false;
}

std::uint8_t straight_high(const std::array<std::uint8_t, 9> &counts) {
  for (int high = 8; high >= 4; --high) {
    bool complete = true;
    for (int rank = high - 4; rank <= high; ++rank) {
      complete = complete && counts[static_cast<std::size_t>(rank)] > 0U;
    }
    if (complete) {
      return static_cast<std::uint8_t>(high);
    }
  }
  if (counts[8] > 0U && counts[0] > 0U && counts[1] > 0U && counts[2] > 0U && counts[3] > 0U) {
    return 3U; // A-6-7-8-9 is nine-high.
  }
  return no_rank;
}

std::array<std::uint8_t, 5> descending_ranks(const std::array<std::uint8_t, 9> &counts,
                                             const std::uint8_t excluded_a = no_rank,
                                             const std::uint8_t excluded_b = no_rank) {
  std::array<std::uint8_t, 5> result{};
  std::size_t output = 0;
  for (int rank = 8; rank >= 0 && output < result.size(); --rank) {
    if (static_cast<std::uint8_t>(rank) == excluded_a ||
        static_cast<std::uint8_t>(rank) == excluded_b) {
      continue;
    }
    for (std::uint8_t count = 0;
         count < counts[static_cast<std::size_t>(rank)] && output < result.size(); ++count) {
      result[output++] = static_cast<std::uint8_t>(rank);
    }
  }
  return result;
}

HandValue evaluate_five_unchecked(const std::array<CardId, 5> &cards);

Result<HandValue, EquityError> evaluate_seven_exact(const std::array<CardId, 7> &cards) {
  if (has_duplicate(cards)) {
    return Result<HandValue, EquityError>::failure(EquityError::DuplicateCard);
  }

  HandValue best{};
  bool initialized = false;
  for (std::size_t first = 0; first < cards.size() - 1; ++first) {
    for (std::size_t second = first + 1; second < cards.size(); ++second) {
      std::array<CardId, 5> five{};
      std::size_t output = 0;
      for (std::size_t index = 0; index < cards.size(); ++index) {
        if (index != first && index != second) {
          five[output++] = cards[index];
        }
      }
      const auto value = evaluate_five_unchecked(five);
      if (!initialized || value > best) {
        best = value;
        initialized = true;
      }
    }
  }
  if (!initialized) {
    return Result<HandValue, EquityError>::failure(EquityError::InternalEvaluatorFailure);
  }
  return Result<HandValue, EquityError>::success(best);
}

HandValue evaluate_five_unchecked(const std::array<CardId, 5> &cards) {
  std::array<std::uint8_t, 9> ranks{};
  std::array<std::uint8_t, 4> suits{};
  for (const auto card : cards) {
    ++ranks[static_cast<std::size_t>(card.rank())];
    ++suits[static_cast<std::size_t>(card.suit())];
  }
  const bool flush = std::ranges::any_of(suits, [](const auto count) { return count == 5U; });
  const auto high = straight_high(ranks);
  const bool straight = high != no_rank;
  if (flush && straight) {
    return {HandCategory::StraightFlush, {high, 0, 0, 0, 0}};
  }

  std::uint8_t four = no_rank;
  std::uint8_t three = no_rank;
  std::array<std::uint8_t, 2> pairs{no_rank, no_rank};
  std::size_t pair_count = 0;
  for (int rank = 8; rank >= 0; --rank) {
    const auto count = ranks[static_cast<std::size_t>(rank)];
    if (count == 4U) {
      four = static_cast<std::uint8_t>(rank);
    } else if (count == 3U) {
      three = static_cast<std::uint8_t>(rank);
    } else if (count == 2U && pair_count < pairs.size()) {
      pairs[pair_count++] = static_cast<std::uint8_t>(rank);
    }
  }
  if (four != no_rank) {
    return {HandCategory::FourOfAKind, {four, descending_ranks(ranks, four)[0], 0, 0, 0}};
  }
  if (flush) {
    return {HandCategory::Flush, descending_ranks(ranks)};
  }
  if (three != no_rank && pair_count > 0U) {
    return {HandCategory::FullHouse, {three, pairs[0], 0, 0, 0}};
  }
  if (straight) {
    return {HandCategory::Straight, {high, 0, 0, 0, 0}};
  }
  if (three != no_rank) {
    const auto rest = descending_ranks(ranks, three);
    return {HandCategory::ThreeOfAKind, {three, rest[0], rest[1], 0, 0}};
  }
  if (pair_count >= 2U) {
    const auto rest = descending_ranks(ranks, pairs[0], pairs[1]);
    return {HandCategory::TwoPair, {pairs[0], pairs[1], rest[0], 0, 0}};
  }
  if (pair_count == 1U) {
    const auto rest = descending_ranks(ranks, pairs[0]);
    return {HandCategory::Pair, {pairs[0], rest[0], rest[1], rest[2], 0}};
  }
  return {HandCategory::HighCard, descending_ranks(ranks)};
}

} // namespace

Result<HandValue, EquityError> evaluate_five(const std::array<CardId, 5> &cards) {
  if (has_duplicate(cards)) {
    return Result<HandValue, EquityError>::failure(EquityError::DuplicateCard);
  }
  return Result<HandValue, EquityError>::success(evaluate_five_unchecked(cards));
}

Result<HandValue, EquityError>
ExactHandEvaluator::evaluate_seven(const std::array<CardId, 7> &cards) const {
  return evaluate_seven_exact(cards);
}

Result<std::vector<HandValue>, EquityError>
ExactHandEvaluator::evaluate_batch(const std::span<const std::array<CardId, 7>> hands) const {
  std::vector<HandValue> values;
  values.reserve(hands.size());
  for (const auto &hand : hands) {
    const auto value = evaluate_seven_exact(hand);
    if (!value) {
      return Result<std::vector<HandValue>, EquityError>::failure(value.error());
    }
    values.push_back(value.value());
  }
  return Result<std::vector<HandValue>, EquityError>::success(std::move(values));
}

Result<HandValue, EquityError> evaluate_seven(const std::array<CardId, 7> &cards) {
  static const ExactHandEvaluator evaluator;
  return evaluator.evaluate_seven(cards);
}

Result<std::vector<HandValue>, EquityError>
evaluate_seven_batch(const std::span<const std::array<CardId, 7>> hands) {
  static const ExactHandEvaluator evaluator;
  return evaluator.evaluate_batch(hands);
}

} // namespace gtosd
