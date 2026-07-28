#pragma once

#include "gtosd/core/result.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gtosd {

enum class Rank : std::uint8_t { Six, Seven, Eight, Nine, Ten, Jack, Queen, King, Ace };
enum class Suit : std::uint8_t { Clubs, Diamonds, Hearts, Spades };
enum class CardError : std::uint8_t { InvalidCard, DuplicateCard };

class CardId {
public:
  constexpr CardId() = default;
  [[nodiscard]] static Result<CardId, CardError> from_index(std::uint8_t value);
  [[nodiscard]] static constexpr CardId from_parts(Rank rank, Suit suit) noexcept {
    return CardId(static_cast<std::uint8_t>(static_cast<std::uint8_t>(rank) * 4U +
                                            static_cast<std::uint8_t>(suit)));
  }

  [[nodiscard]] constexpr std::uint8_t value() const noexcept { return value_; }
  [[nodiscard]] constexpr Rank rank() const noexcept { return static_cast<Rank>(value_ / 4U); }
  [[nodiscard]] constexpr Suit suit() const noexcept { return static_cast<Suit>(value_ % 4U); }
  [[nodiscard]] constexpr std::uint64_t mask() const noexcept { return std::uint64_t{1} << value_; }
  friend constexpr bool operator==(CardId, CardId) noexcept = default;
  friend constexpr auto operator<=>(CardId, CardId) noexcept = default;

private:
  explicit constexpr CardId(std::uint8_t value) noexcept : value_(value) {}
  std::uint8_t value_{0};
};

[[nodiscard]] Result<CardId, CardError> parse_card(std::string_view text);
[[nodiscard]] std::string format_card(CardId card);
[[nodiscard]] std::array<CardId, 36> short_deck();
[[nodiscard]] Result<std::uint64_t, CardError> card_mask(const std::vector<CardId> &cards);
[[nodiscard]] Result<std::uint64_t, CardError>
remaining_deck_mask(const std::vector<CardId> &dead_cards);

} // namespace gtosd
