#pragma once

// Internal layout and inner loops of the multiway kernels (multiway_kernels.cpp).
//
// The inner loops exist twice: scalar (multiway_kernels.cpp, default flags) and AVX2 + FMA
// (multiway_kernels_avx2.cpp, the only file of the library compiled with /arch:AVX2). This header
// deliberately uses no standard-library templates or inline functions, so the AVX2 translation
// unit emits no code that the linker could share with the rest of the library.
#include <cstddef>
#include <cstdint>

namespace gtosd::preflop_blueprint::multiway_detail {

inline constexpr std::size_t deck_cards = 36U;
// Per-hand buffers are padded to a multiple of four doubles (the AVX2 width).
inline constexpr std::size_t padded_hands = 468U;

// A set of live hands with the reach of one seat: per-card sums C[c], the 36x36 pair matrix
// Pm[c][d] = reach of the hand {c, d} when it is in the set (0 otherwise, and on the diagonal),
// and the total. Rows are 288 bytes, so every row of an aligned matrix is 32-byte aligned. The
// size is a multiple of the alignment (no padding warning).
struct alignas(32) CardPairSums {
  double card[deck_cards];
  double pair[deck_cards * deck_cards];
  double total;
  double unused[3];
};

// Sums of the product of the two seats' reach over the hands of a set: per card and total.
struct alignas(32) ProductSums {
  double card[deck_cards];
  double total;
  double unused[3];
};

// For each listed hand h = {x, y} (cards: two bytes per live hand, as BoardContext::cards()) and
// a set S, a_S(c) = S.card[c] - S.pair[x][c] - S.pair[y][c]. The functions write, at index h of
// each output, the sum over all 36 cards c of the named products, the two cards of h included
// (the caller subtracts those two terms).
//   cross_one : a_first * a_second
//   cross_two : a_below * a_fixed, a_group * a_fixed
//   cross_four: a_b1 * a_b2, a_g1 * a_b2, a_b1 * a_g2, a_g1 * a_g2
void cross_one_scalar(const CardPairSums &first, const CardPairSums &second,
                      const std::uint8_t *cards, const std::uint16_t *hands, std::size_t count,
                      double *out) noexcept;
void cross_two_scalar(const CardPairSums &below, const CardPairSums &group,
                      const CardPairSums &fixed, const std::uint8_t *cards,
                      const std::uint16_t *hands, std::size_t count, double *below_out,
                      double *group_out) noexcept;
void cross_four_scalar(const CardPairSums &b1, const CardPairSums &g1, const CardPairSums &b2,
                       const CardPairSums &g2, const std::uint8_t *cards,
                       const std::uint16_t *hands, std::size_t count, double *bb, double *gb,
                       double *bg, double *gg) noexcept;

// Static G form of two full sets (spec 3.4), for every live card a (live: the live cards in
// increasing order), with Pm1 = first.pair and Pm2 = second.pair (symmetric):
//   g[a * 36 + b] = sum over live c of Pm1[a][c] * Pm2[c][b]   (every b; rows of board cards
//                   are not written),
//   q_first[a]    = sum over c of Pm1[a][c] * second.card[c],
//   q_second[a]   = sum over c of Pm2[a][c] * first.card[c].
void pair_product_scalar(const CardPairSums &first, const CardPairSums &second,
                         const std::uint8_t *live, std::size_t live_count, double *g,
                         double *q_first, double *q_second) noexcept;

// The same with AVX2 and FMA. Defined only for x86-64; call only when the CPU supports them.
void pair_product_avx2(const CardPairSums &first, const CardPairSums &second,
                       const std::uint8_t *live, std::size_t live_count, double *g,
                       double *q_first, double *q_second) noexcept;
void cross_one_avx2(const CardPairSums &first, const CardPairSums &second,
                    const std::uint8_t *cards, const std::uint16_t *hands, std::size_t count,
                    double *out) noexcept;
void cross_two_avx2(const CardPairSums &below, const CardPairSums &group,
                    const CardPairSums &fixed, const std::uint8_t *cards,
                    const std::uint16_t *hands, std::size_t count, double *below_out,
                    double *group_out) noexcept;
void cross_four_avx2(const CardPairSums &b1, const CardPairSums &g1, const CardPairSums &b2,
                     const CardPairSums &g2, const std::uint8_t *cards, const std::uint16_t *hands,
                     std::size_t count, double *bb, double *gb, double *bg, double *gg) noexcept;

} // namespace gtosd::preflop_blueprint::multiway_detail
