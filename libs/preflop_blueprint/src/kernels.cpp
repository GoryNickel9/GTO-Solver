#include "gtosd/preflop_blueprint/kernels.hpp"

#include <algorithm>
#include <bit>

namespace gtosd::preflop_blueprint {
namespace {

bool disjoint(const std::array<std::uint8_t, 2> &left,
              const std::array<std::uint8_t, 2> &right) noexcept {
  return left[0] != right[0] && left[0] != right[1] && left[1] != right[0] &&
         left[1] != right[1];
}

} // namespace

void fold_mass(const BoardContext &context, const ConstHandSpan reach,
               const HandSpan disjoint_mass) noexcept {
  const auto cards = context.cards();
  double total = 0.0;
  std::array<double, 36> per_card{};
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const auto mass = reach[hand];
    total += mass;
    per_card[cards[hand][0]] += mass;
    per_card[cards[hand][1]] += mass;
  }
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    disjoint_mass[hand] = total - per_card[cards[hand][0]] - per_card[cards[hand][1]] + reach[hand];
  }
}

void fold_mass_reference(const BoardContext &context, const ConstHandSpan reach,
                         const HandSpan disjoint_mass) noexcept {
  const auto cards = context.cards();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    double total = 0.0;
    for (std::size_t other = 0; other < live_hand_count; ++other) {
      if (other != hand && disjoint(cards[hand], cards[other])) {
        total += reach[other];
      }
    }
    disjoint_mass[hand] = total;
  }
}

void showdown_masses(const BoardContext &context, const ConstHandSpan reach, const HandSpan worse,
                     const HandSpan tied, const HandSpan better) noexcept {
  const auto cards = context.cards();
  const auto ranks = context.ranks();
  const auto order = context.order_by_rank();

  // Ascending pass: mass of the strictly weaker hands, corrected for blockers.
  double below_total = 0.0;
  std::array<double, 36> below_card{};
  std::array<double, 36> group_card{};
  std::size_t start = 0;
  while (start < live_hand_count) {
    std::size_t end = start;
    const auto rank = ranks[order[start]];
    double group_total = 0.0;
    while (end < live_hand_count && ranks[order[end]] == rank) {
      const auto hand = order[end];
      group_total += reach[hand];
      group_card[cards[hand][0]] += reach[hand];
      group_card[cards[hand][1]] += reach[hand];
      ++end;
    }
    for (std::size_t position = start; position < end; ++position) {
      const auto hand = order[position];
      const auto first = cards[hand][0];
      const auto second = cards[hand][1];
      worse[hand] = below_total - below_card[first] - below_card[second];
      tied[hand] = group_total - group_card[first] - group_card[second] + reach[hand];
    }
    for (std::size_t position = start; position < end; ++position) {
      const auto hand = order[position];
      below_total += reach[hand];
      below_card[cards[hand][0]] += reach[hand];
      below_card[cards[hand][1]] += reach[hand];
      group_card[cards[hand][0]] = 0.0;
      group_card[cards[hand][1]] = 0.0;
    }
    start = end;
  }

  // Descending pass: mass of the strictly stronger hands.
  double above_total = 0.0;
  std::array<double, 36> above_card{};
  std::size_t stop = live_hand_count;
  while (stop > 0U) {
    std::size_t begin = stop;
    const auto rank = ranks[order[stop - 1U]];
    while (begin > 0U && ranks[order[begin - 1U]] == rank) {
      --begin;
    }
    for (std::size_t position = begin; position < stop; ++position) {
      const auto hand = order[position];
      better[hand] = above_total - above_card[cards[hand][0]] - above_card[cards[hand][1]];
    }
    for (std::size_t position = begin; position < stop; ++position) {
      const auto hand = order[position];
      above_total += reach[hand];
      above_card[cards[hand][0]] += reach[hand];
      above_card[cards[hand][1]] += reach[hand];
    }
    stop = begin;
  }
}

void showdown_masses_reference(const BoardContext &context, const ConstHandSpan reach,
                               const HandSpan worse, const HandSpan tied,
                               const HandSpan better) noexcept {
  const auto cards = context.cards();
  const auto ranks = context.ranks();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    double below = 0.0;
    double equal = 0.0;
    double above = 0.0;
    for (std::size_t other = 0; other < live_hand_count; ++other) {
      if (other == hand || !disjoint(cards[hand], cards[other])) {
        continue;
      }
      if (ranks[other] < ranks[hand]) {
        below += reach[other];
      } else if (ranks[other] > ranks[hand]) {
        above += reach[other];
      } else {
        equal += reach[other];
      }
    }
    worse[hand] = below;
    tied[hand] = equal;
    better[hand] = above;
  }
}

Result<AllInEquityCache, KernelError>
AllInEquityCache::build(const BoardContext &context, const card_abstraction::AllInTable &table) {
  AllInEquityCache cache;
  const auto rebuilt = cache.rebuild(context, table);
  if (!rebuilt)
    return Result<AllInEquityCache, KernelError>::failure(rebuilt.error());
  return Result<AllInEquityCache, KernelError>::success(std::move(cache));
}

Result<bool, KernelError>
AllInEquityCache::rebuild(const BoardContext &context,
                          const card_abstraction::AllInTable &table) {
  win_.resize(live_hand_count * live_hand_count);
  tie_.resize(live_hand_count * live_hand_count);
  std::fill(win_.begin(), win_.end(), 0.0);
  std::fill(tie_.begin(), tie_.end(), 0.0);
  const auto cards = context.cards();
  const auto combos = context.combo_ids();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    for (std::size_t other = hand + 1U; other < live_hand_count; ++other) {
      if (!disjoint(cards[hand], cards[other])) {
        continue;
      }
      const auto outcome = table.outcome(combos[hand], combos[other]);
      const auto total = outcome.total();
      if (total == 0U) {
        return Result<bool, KernelError>::failure(KernelError::InvalidInput);
      }
      const auto forward = hand * live_hand_count + other;
      const auto reverse = other * live_hand_count + hand;
      win_[forward] = static_cast<double>(outcome.wins) / total;
      win_[reverse] = static_cast<double>(outcome.losses) / total;
      const auto tie = static_cast<double>(outcome.ties) / total;
      tie_[forward] = tie;
      tie_[reverse] = tie;
    }
  }
  return Result<bool, KernelError>::success(true);
}

Result<bool, KernelError>
AllInEquityCache::rebuild(const BoardContext &context,
                          const std::span<const double> win_probability,
                          const std::span<const double> tie_probability) {
  constexpr std::size_t dense_entries = card_abstraction::combo_count *
                                         card_abstraction::combo_count;
  if (win_probability.size() != dense_entries || tie_probability.size() != dense_entries)
    return Result<bool, KernelError>::failure(KernelError::InvalidInput);
  win_.resize(live_hand_count * live_hand_count);
  tie_.resize(live_hand_count * live_hand_count);
  const auto combos = context.combo_ids();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const auto source = static_cast<std::size_t>(combos[hand]) * card_abstraction::combo_count;
    const auto target = hand * live_hand_count;
    for (std::size_t other = 0; other < live_hand_count; ++other) {
      const auto index = source + combos[other];
      win_[target + other] = win_probability[index];
      tie_[target + other] = tie_probability[index];
    }
  }
  return Result<bool, KernelError>::success(true);
}

void AllInEquityCache::masses(const BoardContext &context, const ConstHandSpan reach,
                              const HandSpan win, const HandSpan tie,
                              const HandSpan lose) const noexcept {
  fold_mass(context, reach, lose);
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double *win_row = win_.data() + hand * live_hand_count;
    const double *tie_row = tie_.data() + hand * live_hand_count;
    double win_mass = 0.0;
    double tie_mass = 0.0;
    for (std::size_t other = 0; other < live_hand_count; ++other) {
      win_mass += win_row[other] * reach[other];
      tie_mass += tie_row[other] * reach[other];
    }
    win[hand] = win_mass;
    tie[hand] = tie_mass;
    lose[hand] -= win_mass + tie_mass;
  }
}

void AllInEquityCache::masses_reference(const BoardContext &context,
                                        const card_abstraction::AllInTable &table,
                                        const ConstHandSpan reach, const HandSpan win,
                                        const HandSpan tie, const HandSpan lose) const noexcept {
  const auto cards = context.cards();
  const auto combos = context.combo_ids();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    double win_mass = 0.0;
    double tie_mass = 0.0;
    double lose_mass = 0.0;
    for (std::size_t other = 0; other < live_hand_count; ++other) {
      if (other == hand || !disjoint(cards[hand], cards[other])) {
        continue;
      }
      const auto outcome = table.outcome(combos[hand], combos[other]);
      const auto total = static_cast<double>(outcome.total());
      win_mass += reach[other] * outcome.wins / total;
      tie_mass += reach[other] * outcome.ties / total;
      lose_mass += reach[other] * outcome.losses / total;
    }
    win[hand] = win_mass;
    tie[hand] = tie_mass;
    lose[hand] = lose_mass;
  }
}

void HeadsUpShowdownKernel::evaluate(const BoardContext &context, const std::uint8_t live_mask,
                                     const std::uint8_t hero,
                                     const std::span<const ConstHandSpan> reach,
                                     const HandSpan worse, const HandSpan tied,
                                     const HandSpan better) const noexcept {
  const auto opponents = static_cast<std::uint8_t>(live_mask & ~(std::uint8_t{1} << hero));
  if (std::popcount(opponents) != 1 || reach.size() <= static_cast<std::size_t>(std::countr_zero(opponents))) {
    std::fill(worse.begin(), worse.end(), 0.0);
    std::fill(tied.begin(), tied.end(), 0.0);
    std::fill(better.begin(), better.end(), 0.0);
    return;
  }
  const auto opponent = static_cast<std::size_t>(std::countr_zero(opponents));
  showdown_masses(context, reach[opponent], worse, tied, better);
}

} // namespace gtosd::preflop_blueprint
