#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Exact showdown outcome counts for all live hands on a fixed board.
//
// For every hero hand the counts cover every other hand that is disjoint from
// it (406 opponents out of 465 live hands). The sweep implementation sorts the
// hands by rank once and corrects for card blockers with per-card running
// counts, so a board costs O(n log n) instead of O(n^2). The pairwise
// implementation is the reference used by tests and by the river features,
// where an arbitrary opponent subset is needed.
namespace gtosd::card_abstraction {

struct HandOutcomeCounts {
  std::uint16_t wins{0U};
  std::uint16_t ties{0U};
  std::uint16_t losses{0U};

  [[nodiscard]] constexpr std::uint32_t total() const noexcept {
    return static_cast<std::uint32_t>(wins) + ties + losses;
  }
  [[nodiscard]] constexpr double equity() const noexcept {
    const auto count = total();
    return count == 0U ? 0.5 : (static_cast<double>(wins) + 0.5 * ties) / count;
  }
};

// hands[i] = {lower card index, higher card index}; ranks[i] = ordinal rank of
// hand i on the board; outcomes[i] receives hand i's counts against every
// other listed hand disjoint from it. All spans must have the same size.
void count_showdown_outcomes(std::span<const std::array<std::uint8_t, 2>> hands,
                             std::span<const std::uint16_t> ranks,
                             std::span<HandOutcomeCounts> outcomes);

// Reference implementation: hero hands against an explicit opponent list.
// A hero hand listed among the opponents is skipped as its own opponent.
void count_showdown_outcomes_pairwise(std::span<const std::array<std::uint8_t, 2>> hero_hands,
                                      std::span<const std::uint16_t> hero_ranks,
                                      std::span<const std::array<std::uint8_t, 2>> opponent_hands,
                                      std::span<const std::uint16_t> opponent_ranks,
                                      std::span<HandOutcomeCounts> outcomes);

// The 630 combos in gtosd::all_combos() order as card index pairs and masks.
struct ComboTable {
  std::array<std::array<std::uint8_t, 2>, 630> cards{};
  std::array<std::uint64_t, 630> masks{};
  std::array<std::uint8_t, 630> hand_class{};
};
[[nodiscard]] const ComboTable &combo_table() noexcept;

// Live hands for a board: combos disjoint from board_mask, in combo order.
struct LiveHands {
  std::vector<std::uint16_t> combo_ids;
  std::vector<std::array<std::uint8_t, 2>> cards;
};
void collect_live_hands(std::uint64_t board_mask, LiveHands &output);

} // namespace gtosd::card_abstraction
