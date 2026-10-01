// AVX2 + FMA inner loops of the multiway kernels (see multiway_kernels_detail.hpp). This is the
// only file of the library compiled with AVX2 enabled; it includes no standard-library template
// and defines no inline function, so no AVX2 code can leak into other translation units.
#include "multiway_kernels_detail.hpp"

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#define GTOSD_MULTIWAY_AVX2 1
#endif

namespace gtosd::preflop_blueprint::multiway_detail {

#if defined(GTOSD_MULTIWAY_AVX2)
namespace {

// (l0 + l2) + (l1 + l3): a fixed order for every call.
double horizontal_sum(const __m256d value) noexcept {
  const __m128d low = _mm256_castpd256_pd128(value);
  const __m128d high = _mm256_extractf128_pd(value, 1);
  const __m128d pair = _mm_add_pd(low, high);
  return _mm_cvtsd_f64(_mm_add_sd(pair, _mm_unpackhi_pd(pair, pair)));
}

// a_S(c..c+3) = S.card - S.pair[x] - S.pair[y].
__m256d excluded(const CardPairSums &set, const double *row_x, const double *row_y,
                 const std::size_t card) noexcept {
  return _mm256_sub_pd(_mm256_sub_pd(_mm256_loadu_pd(set.card + card), _mm256_loadu_pd(row_x + card)),
                       _mm256_loadu_pd(row_y + card));
}

} // namespace

void cross_one_avx2(const CardPairSums &first, const CardPairSums &second,
                    const std::uint8_t *cards, const std::uint16_t *hands, const std::size_t count,
                    double *out) noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t hand = hands[index];
    const std::size_t x = cards[2U * hand];
    const std::size_t y = cards[2U * hand + 1U];
    const double *first_x = first.pair + x * deck_cards;
    const double *first_y = first.pair + y * deck_cards;
    const double *second_x = second.pair + x * deck_cards;
    const double *second_y = second.pair + y * deck_cards;
    __m256d sum = _mm256_setzero_pd();
    for (std::size_t card = 0; card < deck_cards; card += 4U) {
      sum = _mm256_fmadd_pd(excluded(first, first_x, first_y, card),
                            excluded(second, second_x, second_y, card), sum);
    }
    out[hand] = horizontal_sum(sum);
  }
}

void cross_two_avx2(const CardPairSums &below, const CardPairSums &group,
                    const CardPairSums &fixed, const std::uint8_t *cards,
                    const std::uint16_t *hands, const std::size_t count, double *below_out,
                    double *group_out) noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t hand = hands[index];
    const std::size_t x = cards[2U * hand];
    const std::size_t y = cards[2U * hand + 1U];
    const double *below_x = below.pair + x * deck_cards;
    const double *below_y = below.pair + y * deck_cards;
    const double *group_x = group.pair + x * deck_cards;
    const double *group_y = group.pair + y * deck_cards;
    const double *fixed_x = fixed.pair + x * deck_cards;
    const double *fixed_y = fixed.pair + y * deck_cards;
    __m256d below_sum = _mm256_setzero_pd();
    __m256d group_sum = _mm256_setzero_pd();
    for (std::size_t card = 0; card < deck_cards; card += 4U) {
      const __m256d f = excluded(fixed, fixed_x, fixed_y, card);
      below_sum = _mm256_fmadd_pd(excluded(below, below_x, below_y, card), f, below_sum);
      group_sum = _mm256_fmadd_pd(excluded(group, group_x, group_y, card), f, group_sum);
    }
    below_out[hand] = horizontal_sum(below_sum);
    group_out[hand] = horizontal_sum(group_sum);
  }
}

void cross_four_avx2(const CardPairSums &b1, const CardPairSums &g1, const CardPairSums &b2,
                     const CardPairSums &g2, const std::uint8_t *cards, const std::uint16_t *hands,
                     const std::size_t count, double *bb, double *gb, double *bg,
                     double *gg) noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t hand = hands[index];
    const std::size_t x = cards[2U * hand];
    const std::size_t y = cards[2U * hand + 1U];
    const double *b1_x = b1.pair + x * deck_cards;
    const double *b1_y = b1.pair + y * deck_cards;
    const double *g1_x = g1.pair + x * deck_cards;
    const double *g1_y = g1.pair + y * deck_cards;
    const double *b2_x = b2.pair + x * deck_cards;
    const double *b2_y = b2.pair + y * deck_cards;
    const double *g2_x = g2.pair + x * deck_cards;
    const double *g2_y = g2.pair + y * deck_cards;
    __m256d sum_bb = _mm256_setzero_pd();
    __m256d sum_gb = _mm256_setzero_pd();
    __m256d sum_bg = _mm256_setzero_pd();
    __m256d sum_gg = _mm256_setzero_pd();
    for (std::size_t card = 0; card < deck_cards; card += 4U) {
      const __m256d a_below = excluded(b1, b1_x, b1_y, card);
      const __m256d a_group = excluded(g1, g1_x, g1_y, card);
      const __m256d b_below = excluded(b2, b2_x, b2_y, card);
      const __m256d b_group = excluded(g2, g2_x, g2_y, card);
      sum_bb = _mm256_fmadd_pd(a_below, b_below, sum_bb);
      sum_gb = _mm256_fmadd_pd(a_group, b_below, sum_gb);
      sum_bg = _mm256_fmadd_pd(a_below, b_group, sum_bg);
      sum_gg = _mm256_fmadd_pd(a_group, b_group, sum_gg);
    }
    bb[hand] = horizontal_sum(sum_bb);
    gb[hand] = horizontal_sum(sum_gb);
    bg[hand] = horizontal_sum(sum_bg);
    gg[hand] = horizontal_sum(sum_gg);
  }
}

#else

// Not x86-64: the dispatcher never selects AVX2; forward to the scalar loops.
void cross_one_avx2(const CardPairSums &first, const CardPairSums &second,
                    const std::uint8_t *cards, const std::uint16_t *hands, const std::size_t count,
                    double *out) noexcept {
  cross_one_scalar(first, second, cards, hands, count, out);
}
void cross_two_avx2(const CardPairSums &below, const CardPairSums &group,
                    const CardPairSums &fixed, const std::uint8_t *cards,
                    const std::uint16_t *hands, const std::size_t count, double *below_out,
                    double *group_out) noexcept {
  cross_two_scalar(below, group, fixed, cards, hands, count, below_out, group_out);
}
void cross_four_avx2(const CardPairSums &b1, const CardPairSums &g1, const CardPairSums &b2,
                     const CardPairSums &g2, const std::uint8_t *cards, const std::uint16_t *hands,
                     const std::size_t count, double *bb, double *gb, double *bg,
                     double *gg) noexcept {
  cross_four_scalar(b1, g1, b2, g2, cards, hands, count, bb, gb, bg, gg);
}

#endif

} // namespace gtosd::preflop_blueprint::multiway_detail
