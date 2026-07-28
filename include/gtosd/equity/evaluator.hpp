#pragma once

#include "gtosd/core/cards.hpp"
#include "gtosd/core/result.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <span>
#include <vector>

namespace gtosd {

enum class HandCategory : std::uint8_t {
  HighCard,
  Pair,
  TwoPair,
  ThreeOfAKind,
  Straight,
  FullHouse,
  Flush,
  FourOfAKind,
  StraightFlush
};

struct HandValue {
  HandCategory category{HandCategory::HighCard};
  std::array<std::uint8_t, 5> kickers{};
  friend auto operator<=>(const HandValue &, const HandValue &) = default;
};

enum class EquityError : std::uint8_t {
  InvalidCard,
  DuplicateCard,
  WrongBoardSize,
  WrongHoleCardCount,
  UnsupportedPlayerCount,
  InternalEvaluatorFailure
};

class IHandEvaluator {
public:
  virtual ~IHandEvaluator() = default;

  [[nodiscard]] virtual Result<HandValue, EquityError>
  evaluate_seven(const std::array<CardId, 7> &cards) const = 0;

  [[nodiscard]] virtual Result<std::vector<HandValue>, EquityError>
  evaluate_batch(std::span<const std::array<CardId, 7>> hands) const = 0;
};

class ExactHandEvaluator final : public IHandEvaluator {
public:
  [[nodiscard]] Result<HandValue, EquityError>
  evaluate_seven(const std::array<CardId, 7> &cards) const override;

  [[nodiscard]] Result<std::vector<HandValue>, EquityError>
  evaluate_batch(std::span<const std::array<CardId, 7>> hands) const override;
};

[[nodiscard]] Result<HandValue, EquityError> evaluate_five(const std::array<CardId, 5> &cards);
[[nodiscard]] Result<HandValue, EquityError> evaluate_seven(const std::array<CardId, 7> &cards);
[[nodiscard]] Result<std::vector<HandValue>, EquityError>
evaluate_seven_batch(std::span<const std::array<CardId, 7>> hands);

} // namespace gtosd
