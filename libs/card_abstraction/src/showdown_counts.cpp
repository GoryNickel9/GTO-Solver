#include "gtosd/card_abstraction/showdown_counts.hpp"

#include "gtosd/core/ranges.hpp"

#include <algorithm>
#include <numeric>

namespace gtosd::card_abstraction {
namespace {

ComboTable make_combo_table() {
  ComboTable table;
  const auto combos = all_combos();
  for (std::size_t index = 0U; index < combos.size(); ++index) {
    const auto first = combos[index].first.value();
    const auto second = combos[index].second.value();
    table.cards[index] = {std::min(first, second), std::max(first, second)};
    table.masks[index] = combos[index].first.mask() | combos[index].second.mask();
    table.hand_class[index] = hand_class(combos[index]);
  }
  return table;
}

const ComboTable combo_table_instance = make_combo_table();

} // namespace

const ComboTable &combo_table() noexcept { return combo_table_instance; }

void collect_live_hands(const std::uint64_t board_mask, LiveHands &output) {
  output.combo_ids.clear();
  output.cards.clear();
  const auto &table = combo_table();
  for (std::size_t index = 0U; index < table.masks.size(); ++index) {
    if ((table.masks[index] & board_mask) == 0U) {
      output.combo_ids.push_back(static_cast<std::uint16_t>(index));
      output.cards.push_back(table.cards[index]);
    }
  }
}

void count_showdown_outcomes(const std::span<const std::array<std::uint8_t, 2>> hands,
                             const std::span<const std::uint16_t> ranks,
                             const std::span<HandOutcomeCounts> outcomes) {
  const auto count = hands.size();
  std::vector<std::uint16_t> order(count);
  std::iota(order.begin(), order.end(), static_cast<std::uint16_t>(0U));
  std::sort(order.begin(), order.end(), [&](const std::uint16_t left, const std::uint16_t right) {
    return ranks[left] < ranks[right];
  });

  // Ascending sweep: hands strictly below the current rank group.
  std::array<std::uint16_t, 36> below_by_card{};
  std::uint16_t below_total = 0U;
  std::size_t begin = 0U;
  while (begin < count) {
    std::size_t end = begin + 1U;
    while (end < count && ranks[order[end]] == ranks[order[begin]]) {
      ++end;
    }
    std::array<std::uint16_t, 36> group_by_card{};
    for (std::size_t position = begin; position < end; ++position) {
      const auto &hand = hands[order[position]];
      ++group_by_card[hand[0]];
      ++group_by_card[hand[1]];
    }
    const auto group_size = static_cast<std::uint16_t>(end - begin);
    for (std::size_t position = begin; position < end; ++position) {
      const auto index = order[position];
      const auto &hand = hands[index];
      outcomes[index].wins =
          static_cast<std::uint16_t>(below_total - below_by_card[hand[0]] - below_by_card[hand[1]]);
      outcomes[index].ties = static_cast<std::uint16_t>(group_size + 1U - group_by_card[hand[0]] -
                                                        group_by_card[hand[1]]);
    }
    for (std::size_t position = begin; position < end; ++position) {
      const auto &hand = hands[order[position]];
      ++below_by_card[hand[0]];
      ++below_by_card[hand[1]];
    }
    below_total = static_cast<std::uint16_t>(below_total + group_size);
    begin = end;
  }

  // Descending sweep: hands strictly above the current rank group.
  std::array<std::uint16_t, 36> above_by_card{};
  std::uint16_t above_total = 0U;
  std::size_t group_end = count;
  while (group_end > 0U) {
    std::size_t group_begin = group_end - 1U;
    while (group_begin > 0U && ranks[order[group_begin - 1U]] == ranks[order[group_end - 1U]]) {
      --group_begin;
    }
    for (std::size_t position = group_begin; position < group_end; ++position) {
      const auto index = order[position];
      const auto &hand = hands[index];
      outcomes[index].losses =
          static_cast<std::uint16_t>(above_total - above_by_card[hand[0]] - above_by_card[hand[1]]);
    }
    for (std::size_t position = group_begin; position < group_end; ++position) {
      const auto &hand = hands[order[position]];
      ++above_by_card[hand[0]];
      ++above_by_card[hand[1]];
    }
    above_total = static_cast<std::uint16_t>(above_total + (group_end - group_begin));
    group_end = group_begin;
  }
}

void count_showdown_outcomes_pairwise(
    const std::span<const std::array<std::uint8_t, 2>> hero_hands,
    const std::span<const std::uint16_t> hero_ranks,
    const std::span<const std::array<std::uint8_t, 2>> opponent_hands,
    const std::span<const std::uint16_t> opponent_ranks,
    const std::span<HandOutcomeCounts> outcomes) {
  for (std::size_t hero = 0U; hero < hero_hands.size(); ++hero) {
    HandOutcomeCounts counts;
    const auto &h = hero_hands[hero];
    for (std::size_t opponent = 0U; opponent < opponent_hands.size(); ++opponent) {
      const auto &o = opponent_hands[opponent];
      if (o[0] == h[0] || o[0] == h[1] || o[1] == h[0] || o[1] == h[1]) {
        continue;
      }
      if (hero_ranks[hero] > opponent_ranks[opponent]) {
        ++counts.wins;
      } else if (hero_ranks[hero] == opponent_ranks[opponent]) {
        ++counts.ties;
      } else {
        ++counts.losses;
      }
    }
    outcomes[hero] = counts;
  }
}

} // namespace gtosd::card_abstraction
