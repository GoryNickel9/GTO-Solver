#pragma once

#include "gtosd/equity/evaluator.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace gtosd {

struct ShowdownWinners {
  std::uint8_t winner_mask{0};
  std::vector<HandValue> values;
};

[[nodiscard]] Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::vector<CardId>> &hole_cards,
                  const std::vector<CardId> &board, const IHandEvaluator &evaluator);

[[nodiscard]] Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::vector<CardId>> &hole_cards,
                  const std::vector<CardId> &board);

[[nodiscard]] Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::array<CardId, 2>> &hole_cards,
                  const std::vector<CardId> &board, const IHandEvaluator &evaluator);

[[nodiscard]] Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::array<CardId, 2>> &hole_cards,
                  const std::vector<CardId> &board);

} // namespace gtosd
