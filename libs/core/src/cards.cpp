#include "gtosd/core/cards.hpp"

#include <algorithm>

namespace gtosd {

Result<CardId, CardError> CardId::from_index(const std::uint8_t value) {
  if (value >= 36U) {
    return Result<CardId, CardError>::failure(CardError::InvalidCard);
  }
  return Result<CardId, CardError>::success(CardId(value));
}

Result<CardId, CardError> parse_card(const std::string_view text) {
  if (text.size() != 2U) {
    return Result<CardId, CardError>::failure(CardError::InvalidCard);
  }
  constexpr std::string_view ranks = "6789TJQKA";
  constexpr std::string_view suits = "cdhs";
  const auto rank_pos = ranks.find(text[0]);
  const auto suit_pos = suits.find(text[1]);
  if (rank_pos == std::string_view::npos || suit_pos == std::string_view::npos) {
    return Result<CardId, CardError>::failure(CardError::InvalidCard);
  }
  return Result<CardId, CardError>::success(
      CardId::from_parts(static_cast<Rank>(rank_pos), static_cast<Suit>(suit_pos)));
}

std::string format_card(const CardId card) {
  constexpr std::string_view ranks = "6789TJQKA";
  constexpr std::string_view suits = "cdhs";
  return {ranks[static_cast<std::size_t>(card.rank())],
          suits[static_cast<std::size_t>(card.suit())]};
}

std::array<CardId, 36> short_deck() {
  std::array<CardId, 36> deck{};
  for (std::uint8_t index = 0; index < 36U; ++index) {
    deck[index] = CardId::from_index(index).value();
  }
  return deck;
}

Result<std::uint64_t, CardError> card_mask(const std::vector<CardId> &cards) {
  std::uint64_t mask = 0;
  for (const auto card : cards) {
    if ((mask & card.mask()) != 0U) {
      return Result<std::uint64_t, CardError>::failure(CardError::DuplicateCard);
    }
    mask |= card.mask();
  }
  return Result<std::uint64_t, CardError>::success(mask);
}

Result<std::uint64_t, CardError> remaining_deck_mask(const std::vector<CardId> &dead_cards) {
  constexpr std::uint64_t full_deck_mask = (std::uint64_t{1} << 36U) - 1U;
  const auto dead_mask = card_mask(dead_cards);
  if (!dead_mask) {
    return Result<std::uint64_t, CardError>::failure(dead_mask.error());
  }
  return Result<std::uint64_t, CardError>::success(full_deck_mask ^ dead_mask.value());
}

} // namespace gtosd
