#include "gtosd/equity/showdown.hpp"

#include <algorithm>
#include <exception>

namespace gtosd {
namespace {

Result<ShowdownWinners, EquityError>
evaluate_showdown_impl(const std::vector<std::vector<CardId>> &hole_cards,
                       const std::vector<CardId> &board, const IHandEvaluator &evaluator) {
  if (board.size() != 5U) {
    return Result<ShowdownWinners, EquityError>::failure(EquityError::WrongBoardSize);
  }
  if (hole_cards.size() < 2U || hole_cards.size() > 6U) {
    return Result<ShowdownWinners, EquityError>::failure(EquityError::UnsupportedPlayerCount);
  }

  std::vector<CardId> all_cards = board;
  for (const auto &hand : hole_cards) {
    if (hand.size() != 2U) {
      return Result<ShowdownWinners, EquityError>::failure(EquityError::WrongHoleCardCount);
    }
    all_cards.insert(all_cards.end(), hand.begin(), hand.end());
  }
  if (!card_mask(all_cards)) {
    return Result<ShowdownWinners, EquityError>::failure(EquityError::DuplicateCard);
  }

  ShowdownWinners result;
  result.values.reserve(hole_cards.size());
  try {
    for (const auto &hand : hole_cards) {
      const std::array<CardId, 7> seven{hand[0],  hand[1],  board[0], board[1],
                                        board[2], board[3], board[4]};
      const auto value = evaluator.evaluate_seven(seven);
      if (!value) {
        return Result<ShowdownWinners, EquityError>::failure(value.error());
      }
      result.values.push_back(value.value());
    }
  } catch (const std::exception &) {
    return Result<ShowdownWinners, EquityError>::failure(EquityError::InternalEvaluatorFailure);
  } catch (...) {
    return Result<ShowdownWinners, EquityError>::failure(EquityError::InternalEvaluatorFailure);
  }

  const auto best = *std::ranges::max_element(result.values);
  for (std::size_t player = 0; player < result.values.size(); ++player) {
    if (result.values[player] == best) {
      result.winner_mask |= static_cast<std::uint8_t>(1U << player);
    }
  }
  if (result.winner_mask == 0U) {
    return Result<ShowdownWinners, EquityError>::failure(EquityError::InternalEvaluatorFailure);
  }
  return Result<ShowdownWinners, EquityError>::success(std::move(result));
}

} // namespace

Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::vector<CardId>> &hole_cards,
                  const std::vector<CardId> &board, const IHandEvaluator &evaluator) {
  return evaluate_showdown_impl(hole_cards, board, evaluator);
}

Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::vector<CardId>> &hole_cards,
                  const std::vector<CardId> &board) {
  static const ExactHandEvaluator evaluator;
  return evaluate_showdown_impl(hole_cards, board, evaluator);
}

Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::array<CardId, 2>> &hole_cards,
                  const std::vector<CardId> &board, const IHandEvaluator &evaluator) {
  std::vector<std::vector<CardId>> dynamic_hands;
  dynamic_hands.reserve(hole_cards.size());
  for (const auto &hand : hole_cards) {
    dynamic_hands.emplace_back(hand.begin(), hand.end());
  }
  return evaluate_showdown_impl(dynamic_hands, board, evaluator);
}

Result<ShowdownWinners, EquityError>
evaluate_showdown(const std::vector<std::array<CardId, 2>> &hole_cards,
                  const std::vector<CardId> &board) {
  static const ExactHandEvaluator evaluator;
  return evaluate_showdown(hole_cards, board, evaluator);
}

} // namespace gtosd
