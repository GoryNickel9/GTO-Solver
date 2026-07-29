#pragma once

#include "gtosd/core/cards.hpp"
#include "gtosd/core/money.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace gtosd {

using ComboId = std::uint16_t;
using HandClassId = std::uint8_t;
using PostflopRange = std::array<RangeWeight, 630>;

struct PostflopRanges {
  std::array<PostflopRange, 2> players{};

  friend bool operator==(const PostflopRanges &, const PostflopRanges &) = default;
};

struct Combo {
  CardId first;
  CardId second;

  friend bool operator==(const Combo &, const Combo &) = default;
};

[[nodiscard]] std::array<Combo, 630> all_combos();
[[nodiscard]] HandClassId hand_class(Combo combo);
[[nodiscard]] std::uint8_t class_mass(HandClassId hand_class_id);
[[nodiscard]] std::string class_name(HandClassId hand_class_id);

} // namespace gtosd
