#include "gtosd/preflop_blueprint/multiway_kernels.hpp"

#include "multiway_kernels_detail.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#include <intrin.h>
#endif

namespace gtosd::preflop_blueprint {

using multiway_detail::CardPairSums;
using multiway_detail::deck_cards;
using multiway_detail::padded_hands;
using multiway_detail::ProductSums;

// Working memory of one thread. Every set is all-zero between kernel calls: the kernels clear
// exactly the entries they wrote.
struct MultiwayScratch::Storage {
  // Three-active: Below and Group of the lower (b1, g1) and higher (b2, g2) seats.
  // Two-active: Below and Group of the opponent (b1, g1), the folded seat's set (b2).
  // Deal mass: the two seats (b1, b2).
  CardPairSums b1;
  CardPairSums g1;
  CardPairSums b2;
  CardPairSums g2;
  // Products of the two vectors over Below and over Group.
  ProductSums below_product;
  ProductSums group_product;
  // Inner-loop outputs, indexed by hand.
  double cross[4][padded_hands];
  // Masses of the value kernels.
  double masses[5][padded_hands];
  // Static G form of the deal mass (spec 3.4): G = Pm1 * Pm2 and the products of each pair
  // matrix with the other set's per-card sums.
  double g[deck_cards * deck_cards];
  double q_first[deck_cards];
  double q_second[deck_cards];
};

namespace {

static_assert(sizeof(std::array<std::uint8_t, 2>) == 2U,
              "BoardContext::cards() is read as two bytes per hand");

bool detect_avx2() noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
  std::array<int, 4> info{};
  __cpuid(info.data(), 0);
  if (info[0] < 7)
    return false;
  __cpuid(info.data(), 1);
  const bool fma = ((info[2] >> 12) & 1) != 0;
  const bool osxsave = ((info[2] >> 27) & 1) != 0;
  const bool avx = ((info[2] >> 28) & 1) != 0;
  if (!fma || !osxsave || !avx)
    return false;
  if ((_xgetbv(0) & 0x6U) != 0x6U)
    return false;
  __cpuidex(info.data(), 7, 0);
  return ((info[1] >> 5) & 1) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
  return false;
#endif
}

const std::uint8_t *card_bytes(const BoardContext &context) noexcept {
  return context.cards().data()->data();
}

void insert(CardPairSums &set, const std::uint8_t x, const std::uint8_t y,
            const double reach) noexcept {
  set.card[x] += reach;
  set.card[y] += reach;
  set.pair[static_cast<std::size_t>(x) * deck_cards + y] = reach;
  set.pair[static_cast<std::size_t>(y) * deck_cards + x] = reach;
  set.total += reach;
}

void insert(ProductSums &set, const std::uint8_t x, const std::uint8_t y,
            const double product) noexcept {
  set.card[x] += product;
  set.card[y] += product;
  set.total += product;
}

// Zero the entries a hand wrote; the total is reset by the caller.
void erase(CardPairSums &set, const std::uint8_t x, const std::uint8_t y) noexcept {
  set.card[x] = 0.0;
  set.card[y] = 0.0;
  set.pair[static_cast<std::size_t>(x) * deck_cards + y] = 0.0;
  set.pair[static_cast<std::size_t>(y) * deck_cards + x] = 0.0;
}

void erase(ProductSums &set, const std::uint8_t x, const std::uint8_t y) noexcept {
  set.card[x] = 0.0;
  set.card[y] = 0.0;
}

// a_S(c) for one card: the reach of the set's hands holding c and neither card of h = {x, y}.
double excluded(const CardPairSums &set, const std::size_t x, const std::size_t y,
                const std::size_t card) noexcept {
  return set.card[card] - set.pair[x * deck_cards + card] - set.pair[y * deck_cards + card];
}

// Product mass of a set disjoint from h; `member` is r1(h) r2(h) when h is in the set, else 0.
double disjoint_total(const ProductSums &set, const std::size_t x, const std::size_t y,
                      const double member) noexcept {
  return set.total - set.card[x] - set.card[y] + member;
}

void cross_one(const MultiwayKernelIsa isa, const CardPairSums &first, const CardPairSums &second,
               const std::uint8_t *cards, const std::uint16_t *hands, const std::size_t count,
               double *out) noexcept {
  if (isa == MultiwayKernelIsa::Avx2)
    multiway_detail::cross_one_avx2(first, second, cards, hands, count, out);
  else
    multiway_detail::cross_one_scalar(first, second, cards, hands, count, out);
}

void cross_two(const MultiwayKernelIsa isa, const CardPairSums &below, const CardPairSums &group,
               const CardPairSums &fixed, const std::uint8_t *cards, const std::uint16_t *hands,
               const std::size_t count, double *below_out, double *group_out) noexcept {
  if (isa == MultiwayKernelIsa::Avx2)
    multiway_detail::cross_two_avx2(below, group, fixed, cards, hands, count, below_out,
                                    group_out);
  else
    multiway_detail::cross_two_scalar(below, group, fixed, cards, hands, count, below_out,
                                      group_out);
}

void cross_four(const MultiwayKernelIsa isa, const CardPairSums &b1, const CardPairSums &g1,
                const CardPairSums &b2, const CardPairSums &g2, const std::uint8_t *cards,
                const std::uint16_t *hands, const std::size_t count, double *bb, double *gb,
                double *bg, double *gg) noexcept {
  if (isa == MultiwayKernelIsa::Avx2)
    multiway_detail::cross_four_avx2(b1, g1, b2, g2, cards, hands, count, bb, gb, bg, gg);
  else
    multiway_detail::cross_four_scalar(b1, g1, b2, g2, cards, hands, count, bb, gb, bg, gg);
}

// deal[h] = M(All, All)[h] for the full sets `first` (reach r1) and `second` (r2), by the static
// G form of spec 3.4: with G = Pm1 * Pm2, Q1 = Pm1 * C2 and Q2 = Pm2 * C1, the 36-card sum is
//   sum_c a1(c) a2(c) = S0 - Q1[x] - Q1[y] - Q2[x] - Q2[y] + G[x][x] + G[x][y] + G[y][x] + G[y][y]
// (S0 = sum_c C1[c] C2[c]); its two terms c in h are (C1[c] - r1(h)) (C2[c] - r2(h)), and the
// product mass of a card is the diagonal of G (Pc[x] = G[x][x]).
void deal_full(const MultiwayKernelIsa isa, const BoardContext &context, const CardPairSums &first,
               const CardPairSums &second, const ConstHandSpan r1, const ConstHandSpan r2,
               MultiwayScratch::Storage &s, const HandSpan deal) noexcept {
  std::array<std::uint8_t, deck_cards> live{};
  std::size_t live_count = 0U;
  for (std::size_t card = 0U; card < deck_cards; ++card)
    if (context.card_is_live(static_cast<std::uint8_t>(card)))
      live[live_count++] = static_cast<std::uint8_t>(card);
  if (isa == MultiwayKernelIsa::Avx2)
    multiway_detail::pair_product_avx2(first, second, live.data(), live_count, s.g, s.q_first,
                                       s.q_second);
  else
    multiway_detail::pair_product_scalar(first, second, live.data(), live_count, s.g, s.q_first,
                                         s.q_second);
  double s0 = 0.0;
  double diagonal = 0.0;
  for (std::size_t index = 0; index < live_count; ++index) {
    const std::size_t card = live[index];
    s0 += first.card[card] * second.card[card];
    diagonal += s.g[card * deck_cards + card];
  }
  const double product_total = 0.5 * diagonal;
  const auto cards = context.cards();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const std::size_t x = cards[hand][0];
    const std::size_t y = cards[hand][1];
    const double r1h = r1[hand];
    const double r2h = r2[hand];
    const double c1x = first.card[x];
    const double c1y = first.card[y];
    const double c2x = second.card[x];
    const double c2y = second.card[y];
    const double gxx = s.g[x * deck_cards + x];
    const double gyy = s.g[y * deck_cards + y];
    const double full = s0 - s.q_first[x] - s.q_first[y] - s.q_second[x] - s.q_second[y] + gxx +
                        s.g[x * deck_cards + y] + s.g[y * deck_cards + x] + gyy;
    const double own = (c1x - r1h) * (c2x - r2h) + (c1y - r1h) * (c2y - r2h);
    const double a = first.total - c1x - c1y + r1h;
    const double b = second.total - c2x - c2y + r2h;
    deal[hand] = a * b - (full - own) + (product_total - gxx - gyy + r1h * r2h);
  }
}

void reset(CardPairSums &set) noexcept {
  std::memset(static_cast<void *>(&set), 0, sizeof(CardPairSums));
}

void reset(ProductSums &set) noexcept {
  std::memset(static_cast<void *>(&set), 0, sizeof(ProductSums));
}

// Clear a set that holds every live hand.
void clear_full(CardPairSums &set, const BoardContext &context) noexcept {
  const auto cards = context.cards();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand)
    erase(set, cards[hand][0], cards[hand][1]);
  set.total = 0.0;
}

void clear_full(ProductSums &set) noexcept {
  std::fill(std::begin(set.card), std::end(set.card), 0.0);
  set.total = 0.0;
}

} // namespace

namespace multiway_detail {

// Scalar loops: four partial sums per product (cards c = 4k + j in lane j), combined as
// (l0 + l2) + (l1 + l3), the order of the AVX2 loops (which round through FMA instead).
namespace {
double combine(const std::array<double, 4> &lanes) noexcept {
  return (lanes[0] + lanes[2]) + (lanes[1] + lanes[3]);
}
} // namespace

void cross_one_scalar(const CardPairSums &first, const CardPairSums &second,
                      const std::uint8_t *cards, const std::uint16_t *hands,
                      const std::size_t count, double *out) noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t hand = hands[index];
    const std::size_t x = cards[2U * hand];
    const std::size_t y = cards[2U * hand + 1U];
    std::array<double, 4> sum{};
    for (std::size_t card = 0; card < deck_cards; card += 4U)
      for (std::size_t lane = 0; lane < 4U; ++lane)
        sum[lane] += excluded(first, x, y, card + lane) * excluded(second, x, y, card + lane);
    out[hand] = combine(sum);
  }
}

void cross_two_scalar(const CardPairSums &below, const CardPairSums &group,
                      const CardPairSums &fixed, const std::uint8_t *cards,
                      const std::uint16_t *hands, const std::size_t count, double *below_out,
                      double *group_out) noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t hand = hands[index];
    const std::size_t x = cards[2U * hand];
    const std::size_t y = cards[2U * hand + 1U];
    std::array<double, 4> below_sum{};
    std::array<double, 4> group_sum{};
    for (std::size_t card = 0; card < deck_cards; card += 4U)
      for (std::size_t lane = 0; lane < 4U; ++lane) {
        const double f = excluded(fixed, x, y, card + lane);
        below_sum[lane] += excluded(below, x, y, card + lane) * f;
        group_sum[lane] += excluded(group, x, y, card + lane) * f;
      }
    below_out[hand] = combine(below_sum);
    group_out[hand] = combine(group_sum);
  }
}

void cross_four_scalar(const CardPairSums &b1, const CardPairSums &g1, const CardPairSums &b2,
                       const CardPairSums &g2, const std::uint8_t *cards,
                       const std::uint16_t *hands, const std::size_t count, double *bb, double *gb,
                       double *bg, double *gg) noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t hand = hands[index];
    const std::size_t x = cards[2U * hand];
    const std::size_t y = cards[2U * hand + 1U];
    std::array<double, 4> sum_bb{};
    std::array<double, 4> sum_gb{};
    std::array<double, 4> sum_bg{};
    std::array<double, 4> sum_gg{};
    for (std::size_t card = 0; card < deck_cards; card += 4U)
      for (std::size_t lane = 0; lane < 4U; ++lane) {
        const double a_below = excluded(b1, x, y, card + lane);
        const double a_group = excluded(g1, x, y, card + lane);
        const double b_below = excluded(b2, x, y, card + lane);
        const double b_group = excluded(g2, x, y, card + lane);
        sum_bb[lane] += a_below * b_below;
        sum_gb[lane] += a_group * b_below;
        sum_bg[lane] += a_below * b_group;
        sum_gg[lane] += a_group * b_group;
      }
    bb[hand] = combine(sum_bb);
    gb[hand] = combine(sum_gb);
    bg[hand] = combine(sum_bg);
    gg[hand] = combine(sum_gg);
  }
}

void pair_product_scalar(const CardPairSums &first, const CardPairSums &second,
                         const std::uint8_t *live, const std::size_t live_count, double *g,
                         double *q_first, double *q_second) noexcept {
  for (std::size_t row = 0; row < live_count; ++row) {
    const std::size_t a = live[row];
    const double *first_row = first.pair + a * deck_cards;
    const double *second_row = second.pair + a * deck_cards;
    double *target = g + a * deck_cards;
    std::fill(target, target + deck_cards, 0.0);
    for (std::size_t column = 0; column < live_count; ++column) {
      const std::size_t c = live[column];
      const double weight = first_row[c];
      if (weight == 0.0)
        continue;
      const double *source = second.pair + c * deck_cards;
      for (std::size_t b = 0; b < deck_cards; ++b)
        target[b] += weight * source[b];
    }
    std::array<double, 4> dot_first{};
    std::array<double, 4> dot_second{};
    for (std::size_t card = 0; card < deck_cards; card += 4U)
      for (std::size_t lane = 0; lane < 4U; ++lane) {
        dot_first[lane] += first_row[card + lane] * second.card[card + lane];
        dot_second[lane] += second_row[card + lane] * first.card[card + lane];
      }
    q_first[a] = combine(dot_first);
    q_second[a] = combine(dot_second);
  }
}

} // namespace multiway_detail

bool multiway_kernel_avx2_available() noexcept {
  static const bool available = detect_avx2();
  return available;
}

MultiwayKernelIsa best_multiway_kernel_isa() noexcept {
  return multiway_kernel_avx2_available() ? MultiwayKernelIsa::Avx2 : MultiwayKernelIsa::Scalar;
}

const char *multiway_kernel_isa_name(const MultiwayKernelIsa isa) noexcept {
  switch (isa) {
  case MultiwayKernelIsa::Scalar:
    return "scalar";
  case MultiwayKernelIsa::Avx2:
    return "avx2";
  }
  return "unknown";
}

MultiwayScratch::MultiwayScratch(const MultiwayKernelIsa isa)
    : isa_(isa == MultiwayKernelIsa::Avx2 && !multiway_kernel_avx2_available()
               ? MultiwayKernelIsa::Scalar
               : isa),
      storage_(std::make_unique<Storage>()) {
  reset(storage_->b1);
  reset(storage_->g1);
  reset(storage_->b2);
  reset(storage_->g2);
  reset(storage_->below_product);
  reset(storage_->group_product);
}

MultiwayScratch::~MultiwayScratch() = default;
MultiwayScratch::MultiwayScratch(MultiwayScratch &&) noexcept = default;
MultiwayScratch &MultiwayScratch::operator=(MultiwayScratch &&) noexcept = default;

std::size_t MultiwayScratch::bytes() noexcept { return sizeof(Storage); }

void three_seat_deal_mass(const BoardContext &context, const ConstHandSpan first,
                          const ConstHandSpan second, const HandSpan deal,
                          MultiwayScratch &scratch) noexcept {
  auto &s = scratch.storage();
  const auto cards = context.cards();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const auto x = cards[hand][0];
    const auto y = cards[hand][1];
    insert(s.b1, x, y, first[hand]);
    insert(s.b2, x, y, second[hand]);
  }
  deal_full(scratch.isa(), context, s.b1, s.b2, first, second, s, deal);
  clear_full(s.b1, context);
  clear_full(s.b2, context);
}

void three_active_masses(const BoardContext &context, const ConstHandSpan lower,
                         const ConstHandSpan higher, const ThreeActiveMassSpans &out,
                         MultiwayScratch &scratch) noexcept {
  auto &s = scratch.storage();
  const auto isa = scratch.isa();
  const auto cards = context.cards();
  const auto order = context.order_by_rank();
  const auto starts = context.rank_group_starts();
  const auto *bytes = card_bytes(context);
  double *bb = s.cross[0];
  double *gb = s.cross[1];
  double *bg = s.cross[2];
  double *gg = s.cross[3];
  for (std::size_t group = 0; group + 1U < starts.size(); ++group) {
    const std::size_t begin = starts[group];
    const std::size_t end = starts[group + 1U];
    const std::size_t count = end - begin;
    for (std::size_t position = begin; position < end; ++position) {
      const auto hand = order[position];
      insert(s.g1, cards[hand][0], cards[hand][1], lower[hand]);
      insert(s.g2, cards[hand][0], cards[hand][1], higher[hand]);
      insert(s.group_product, cards[hand][0], cards[hand][1], lower[hand] * higher[hand]);
    }
    if (count > 1U)
      cross_four(isa, s.b1, s.g1, s.b2, s.g2, bytes, order.data() + begin, count, bb, gb, bg, gg);
    else
      cross_one(isa, s.b1, s.b2, bytes, order.data() + begin, count, bb);
    for (std::size_t position = begin; position < end; ++position) {
      const auto hand = order[position];
      const std::size_t x = cards[hand][0];
      const std::size_t y = cards[hand][1];
      const double r1h = lower[hand];
      const double r2h = higher[hand];
      // h is never in Below; G and B are disjoint, so M(G,B) and M(B,G) have no P term. The two
      // terms c in h of each 36-card sum: a_B(c) = C_B[c] and a_G(c) = C_G[c] - r(h).
      const double b1x = s.b1.card[x];
      const double b1y = s.b1.card[y];
      const double b2x = s.b2.card[x];
      const double b2y = s.b2.card[y];
      const double a_below = s.b1.total - b1x - b1y;
      const double b_below = s.b2.total - b2x - b2y;
      out.win[hand] = a_below * b_below - (bb[hand] - (b1x * b2x + b1y * b2y)) +
                      disjoint_total(s.below_product, x, y, 0.0);
      if (count > 1U) {
        const double g1x = s.g1.card[x] - r1h;
        const double g1y = s.g1.card[y] - r1h;
        const double g2x = s.g2.card[x] - r2h;
        const double g2y = s.g2.card[y] - r2h;
        const double a_group = s.g1.total - s.g1.card[x] - s.g1.card[y] + r1h;
        const double b_group = s.g2.total - s.g2.card[x] - s.g2.card[y] + r2h;
        out.tie_lower[hand] = a_group * b_below - (gb[hand] - (g1x * b2x + g1y * b2y));
        out.tie_higher[hand] = a_below * b_group - (bg[hand] - (b1x * g2x + b1y * g2y));
        out.tie_both[hand] = a_group * b_group - (gg[hand] - (g1x * g2x + g1y * g2y)) +
                             disjoint_total(s.group_product, x, y, r1h * r2h);
      } else {
        // h alone in its group: no other hand ties it.
        out.tie_lower[hand] = 0.0;
        out.tie_higher[hand] = 0.0;
        out.tie_both[hand] = 0.0;
      }
    }
    for (std::size_t position = begin; position < end; ++position) {
      const auto hand = order[position];
      const auto x = cards[hand][0];
      const auto y = cards[hand][1];
      insert(s.b1, x, y, lower[hand]);
      insert(s.b2, x, y, higher[hand]);
      insert(s.below_product, x, y, lower[hand] * higher[hand]);
      erase(s.g1, x, y);
      erase(s.g2, x, y);
      erase(s.group_product, x, y);
    }
    s.g1.total = 0.0;
    s.g2.total = 0.0;
    s.group_product.total = 0.0;
  }
  // Below now holds every live hand: the deal mass, then the losses by difference.
  deal_full(isa, context, s.b1, s.b2, lower, higher, s, out.lose);
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    out.lose[hand] = out.lose[hand] - out.win[hand] - out.tie_lower[hand] - out.tie_higher[hand] -
                     out.tie_both[hand];
  }
  clear_full(s.b1, context);
  clear_full(s.b2, context);
  clear_full(s.below_product);
}

void two_active_masses(const BoardContext &context, const ConstHandSpan opponent,
                       const ConstHandSpan folded, const TwoActiveMassSpans &out,
                       MultiwayScratch &scratch) noexcept {
  auto &s = scratch.storage();
  const auto isa = scratch.isa();
  const auto cards = context.cards();
  const auto order = context.order_by_rank();
  const auto starts = context.rank_group_starts();
  const auto *bytes = card_bytes(context);
  // The folded seat's set is static: every live hand.
  for (std::size_t hand = 0; hand < live_hand_count; ++hand)
    insert(s.b2, cards[hand][0], cards[hand][1], folded[hand]);
  double *win_cross = s.cross[0];
  double *tie_cross = s.cross[1];
  for (std::size_t group = 0; group + 1U < starts.size(); ++group) {
    const std::size_t begin = starts[group];
    const std::size_t end = starts[group + 1U];
    const std::size_t count = end - begin;
    for (std::size_t position = begin; position < end; ++position) {
      const auto hand = order[position];
      insert(s.g1, cards[hand][0], cards[hand][1], opponent[hand]);
      insert(s.group_product, cards[hand][0], cards[hand][1], opponent[hand] * folded[hand]);
    }
    if (count > 1U)
      cross_two(isa, s.b1, s.g1, s.b2, bytes, order.data() + begin, count, win_cross, tie_cross);
    else
      cross_one(isa, s.b1, s.b2, bytes, order.data() + begin, count, win_cross);
    for (std::size_t position = begin; position < end; ++position) {
      const auto hand = order[position];
      const std::size_t x = cards[hand][0];
      const std::size_t y = cards[hand][1];
      const double ro = opponent[hand];
      const double rf = folded[hand];
      // The folded seat's set holds h: a_F(c) = C_F[c] - r_f(h) at c in h.
      const double fx = s.b2.card[x] - rf;
      const double fy = s.b2.card[y] - rf;
      const double fixed = s.b2.total - s.b2.card[x] - s.b2.card[y] + rf;
      const double bx = s.b1.card[x];
      const double by = s.b1.card[y];
      out.win[hand] = (s.b1.total - bx - by) * fixed - (win_cross[hand] - (bx * fx + by * fy)) +
                      disjoint_total(s.below_product, x, y, 0.0);
      if (count > 1U) {
        const double gx = s.g1.card[x] - ro;
        const double gy = s.g1.card[y] - ro;
        out.tie[hand] = (s.g1.total - s.g1.card[x] - s.g1.card[y] + ro) * fixed -
                        (tie_cross[hand] - (gx * fx + gy * fy)) +
                        disjoint_total(s.group_product, x, y, ro * rf);
      } else {
        out.tie[hand] = 0.0;
      }
    }
    for (std::size_t position = begin; position < end; ++position) {
      const auto hand = order[position];
      const auto x = cards[hand][0];
      const auto y = cards[hand][1];
      insert(s.b1, x, y, opponent[hand]);
      insert(s.below_product, x, y, opponent[hand] * folded[hand]);
      erase(s.g1, x, y);
      erase(s.group_product, x, y);
    }
    s.g1.total = 0.0;
    s.group_product.total = 0.0;
  }
  deal_full(isa, context, s.b1, s.b2, opponent, folded, s, out.lose);
  for (std::size_t hand = 0; hand < live_hand_count; ++hand)
    out.lose[hand] = out.lose[hand] - out.win[hand] - out.tie[hand];
  clear_full(s.b1, context);
  clear_full(s.b2, context);
  clear_full(s.below_product);
}

void three_active_values(const BoardContext &context, const ConstHandSpan lower,
                         const ConstHandSpan higher, const ThreeActivePayoffs &payoffs,
                         const HandSpan values, MultiwayScratch &scratch) noexcept {
  auto &s = scratch.storage();
  const ThreeActiveMassSpans masses{
      HandSpan(s.masses[0], live_hand_count), HandSpan(s.masses[1], live_hand_count),
      HandSpan(s.masses[2], live_hand_count), HandSpan(s.masses[3], live_hand_count),
      HandSpan(s.masses[4], live_hand_count)};
  three_active_masses(context, lower, higher, masses, scratch);
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    values[hand] = payoffs.win * masses.win[hand] + payoffs.tie_lower * masses.tie_lower[hand] +
                   payoffs.tie_higher * masses.tie_higher[hand] +
                   payoffs.tie_both * masses.tie_both[hand] + payoffs.lose * masses.lose[hand];
  }
}

void two_active_values(const BoardContext &context, const ConstHandSpan opponent,
                       const ConstHandSpan folded, const TwoActivePayoffs &payoffs,
                       const HandSpan values, MultiwayScratch &scratch) noexcept {
  auto &s = scratch.storage();
  const TwoActiveMassSpans masses{HandSpan(s.masses[0], live_hand_count),
                                  HandSpan(s.masses[1], live_hand_count),
                                  HandSpan(s.masses[2], live_hand_count)};
  two_active_masses(context, opponent, folded, masses, scratch);
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    values[hand] = payoffs.win * masses.win[hand] + payoffs.tie * masses.tie[hand] +
                   payoffs.lose * masses.lose[hand];
  }
}

namespace {

std::array<std::uint64_t, live_hand_count> hand_masks(const BoardContext &context) noexcept {
  std::array<std::uint64_t, live_hand_count> masks{};
  const auto cards = context.cards();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand)
    masks[hand] = (std::uint64_t{1} << cards[hand][0]) | (std::uint64_t{1} << cards[hand][1]);
  return masks;
}

// Reach of `reach` over the hands disjoint from `used`, split by rank against `rank`.
struct RankSplit {
  double below{0.0};
  double equal{0.0};
  double above{0.0};
};

RankSplit split_disjoint(const std::array<std::uint64_t, live_hand_count> &masks,
                         const std::span<const std::uint16_t, live_hand_count> ranks,
                         const ConstHandSpan reach, const std::uint64_t used,
                         const std::uint16_t rank) noexcept {
  RankSplit split;
  for (std::size_t other = 0; other < live_hand_count; ++other) {
    if ((masks[other] & used) != 0U)
      continue;
    if (ranks[other] < rank)
      split.below += reach[other];
    else if (ranks[other] == rank)
      split.equal += reach[other];
    else
      split.above += reach[other];
  }
  return split;
}

} // namespace

void three_seat_deal_mass_reference(const BoardContext &context, const ConstHandSpan first,
                                    const ConstHandSpan second, const HandSpan deal) noexcept {
  const auto masks = hand_masks(context);
  const auto ranks = context.ranks();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    double total = 0.0;
    for (std::size_t one = 0; one < live_hand_count; ++one) {
      if (first[one] == 0.0 || (masks[one] & masks[hand]) != 0U)
        continue;
      const auto split = split_disjoint(masks, ranks, second, masks[hand] | masks[one], 0U);
      total += first[one] * (split.below + split.equal + split.above);
    }
    deal[hand] = total;
  }
}

void three_active_masses_reference(const BoardContext &context, const ConstHandSpan lower,
                                   const ConstHandSpan higher,
                                   const ThreeActiveMassSpans &out) noexcept {
  const auto masks = hand_masks(context);
  const auto ranks = context.ranks();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const auto rank = ranks[hand];
    double win = 0.0;
    double tie_lower = 0.0;
    double tie_higher = 0.0;
    double tie_both = 0.0;
    double lose = 0.0;
    for (std::size_t one = 0; one < live_hand_count; ++one) {
      if (lower[one] == 0.0 || (masks[one] & masks[hand]) != 0U)
        continue;
      const auto split = split_disjoint(masks, ranks, higher, masks[hand] | masks[one], rank);
      const double weight = lower[one];
      if (ranks[one] < rank) {
        win += weight * split.below;
        tie_higher += weight * split.equal;
        lose += weight * split.above;
      } else if (ranks[one] == rank) {
        tie_lower += weight * split.below;
        tie_both += weight * split.equal;
        lose += weight * split.above;
      } else {
        lose += weight * (split.below + split.equal + split.above);
      }
    }
    out.win[hand] = win;
    out.tie_lower[hand] = tie_lower;
    out.tie_higher[hand] = tie_higher;
    out.tie_both[hand] = tie_both;
    out.lose[hand] = lose;
  }
}

void two_active_masses_reference(const BoardContext &context, const ConstHandSpan opponent,
                                 const ConstHandSpan folded,
                                 const TwoActiveMassSpans &out) noexcept {
  const auto masks = hand_masks(context);
  const auto ranks = context.ranks();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    double win = 0.0;
    double tie = 0.0;
    double lose = 0.0;
    for (std::size_t one = 0; one < live_hand_count; ++one) {
      if (opponent[one] == 0.0 || (masks[one] & masks[hand]) != 0U)
        continue;
      const auto split = split_disjoint(masks, ranks, folded, masks[hand] | masks[one], 0U);
      const double mass = opponent[one] * (split.below + split.equal + split.above);
      if (ranks[one] < ranks[hand])
        win += mass;
      else if (ranks[one] == ranks[hand])
        tie += mass;
      else
        lose += mass;
    }
    out.win[hand] = win;
    out.tie[hand] = tie;
    out.lose[hand] = lose;
  }
}

MultiwayKernelFlops multiway_kernel_flops_per_hand() noexcept {
  // Sweeps, per card: a_S(c) is two subtractions, a product-accumulate two flops. The deal mass
  // (G form): G = Pm1 * Pm2 over the 31 live cards (31 x 30 x 36 multiply-adds) and the two
  // products with the card sums (2 x 31 x 36), spread over the 465 hands, plus about 30 flops per
  // hand. The O(1) part of the sweeps is counted approximately: 40 flops for a tied group, 12 for
  // a singleton, 24 for the two-active sweep.
  constexpr double cards = static_cast<double>(deck_cards);
  constexpr double live = 31.0;
  constexpr double hands = static_cast<double>(live_hand_count);
  MultiwayKernelFlops flops;
  flops.deal = 2.0 * (live * (live - 1.0) * cards + 2.0 * live * cards) / hands + 30.0;
  flops.three_active_group = cards * 16.0 + 40.0 + flops.deal + 4.0;
  flops.three_active_singleton = cards * 6.0 + 12.0 + flops.deal + 4.0;
  flops.two_active = cards * 10.0 + 24.0 + flops.deal + 2.0;
  return flops;
}

} // namespace gtosd::preflop_blueprint
