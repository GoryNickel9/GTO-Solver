#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <immintrin.h>
#include <limits>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t card_count = 36U;
constexpr float win_payoff = 10.25F;
constexpr float tie_payoff = -0.5F;
constexpr float loss_payoff = -11.25F;
constexpr float inverse_normalization = 0.03125F;
constexpr std::size_t batch_lane_count = 4U;
constexpr std::array<float, batch_lane_count> lane_win_payoffs{
    10.25F, 7.75F, 15.5F, 4.0F};
constexpr std::array<float, batch_lane_count> lane_tie_payoffs{
    -0.5F, 0.25F, -1.25F, 0.0F};
constexpr std::array<float, batch_lane_count> lane_loss_payoffs{
    -11.25F, -8.5F, -16.0F, -5.0F};
constexpr std::uint16_t invalid_slot =
    std::numeric_limits<std::uint16_t>::max();

struct PayoffTriple {
  double win;
  double tie;
  double loss;
};

constexpr std::array<PayoffTriple, batch_lane_count> production_lane_payoffs{{
    {10.25, -0.5, -11.25},
    {7.75, 0.25, -8.5},
    {15.5, -1.25, -16.0},
    {4.0, 0.0, -5.0},
}};

struct WorkloadShape {
  std::string_view name;
  std::size_t rank_count;
  std::size_t hero_hands;
  std::size_t opponent_hands;
};

constexpr WorkloadShape workload_shapes[] = {
    {"small", 24U, 48U, 48U},
    {"medium", 80U, 220U, 220U},
    {"large", 220U, 500U, 500U},
    // Observed TSTC9D river maxima, in both asymmetric player-pass
    // orientations: rank cells / hero combos / opponent combos.
    {"tst_p0", 36U, 358U, 301U},
    {"tst_p1", 36U, 301U, 358U},
};

struct ComboData {
  std::uint16_t rank{};
  std::uint16_t first_by_rank{};
  std::uint16_t second_by_rank{};
  std::uint16_t first_all{};
  std::uint16_t second_all{};
  std::uint16_t opponent_slot{};
};

struct ComboColumns {
  std::vector<std::uint16_t> rank;
  std::vector<std::uint16_t> first_by_rank;
  std::vector<std::uint16_t> second_by_rank;
  std::vector<std::uint16_t> first_all;
  std::vector<std::uint16_t> second_all;
  std::vector<std::uint16_t> opponent_slot;

  void resize(const std::size_t size) {
    rank.resize(size);
    first_by_rank.resize(size);
    second_by_rank.resize(size);
    first_all.resize(size);
    second_all.resize(size);
    opponent_slot.resize(size);
  }
};

struct Scratch {
  std::vector<float> totals;
  std::vector<float> rank_base;
  std::vector<float> by_card;
  std::vector<float> prefix;
  std::vector<float> card_prefix;
  std::vector<float> output;
};

// Rank/card cells are lane-interleaved. A single metadata lookup therefore
// drives four independent reach vectors while every lane retains the scalar
// accumulation and prefix order of the production-shaped baseline.
struct FourLaneScratch {
  std::vector<float> totals;
  std::vector<float> rank_base;
  std::vector<float> by_card;
  std::vector<float> prefix;
  std::vector<float> card_prefix;
  std::vector<float> output;
};

struct FourLaneAccumScratch {
  std::vector<float> totals;
  std::vector<float> by_card;
};

struct Fixture {
  explicit Fixture(const WorkloadShape shape)
      : shape(shape), opponent(shape.opponent_hands), hero(shape.hero_hands),
        opponent_reach(shape.opponent_hands),
        parent_reach(shape.opponent_hands),
        action_strategy(shape.opponent_hands),
        level1_reference_child(shape.opponent_hands),
        level1_candidate_child(shape.opponent_hands),
        touched(shape.opponent_hands * 2U), baseline(make_scratch()),
        candidate(make_scratch()), four_lane(make_four_lane_scratch()),
        production_batch_accum(make_four_lane_accum_scratch()),
        production_two_card(make_four_lane_scratch()),
        production_shared_reach(make_scratch()),
        level1_reference_summary(make_scratch()),
        level1_candidate_summary(make_scratch()) {
    for (auto &reach : lane_reaches) {
      reach.resize(shape.opponent_hands);
    }
    for (auto &scratch : sequential) {
      scratch = make_scratch();
    }
    for (auto &scratch : production_reference) {
      scratch = make_scratch();
    }
    for (auto &scratch : production_batch_finish) {
      scratch = make_scratch();
    }
    for (auto &scratch : production_same_reach_reference) {
      scratch = make_scratch();
    }
    for (auto &scratch : production_same_reach_shared) {
      scratch = make_scratch();
    }
    for (auto &scratch : level1_reference_output) {
      scratch = make_scratch();
    }
    for (auto &scratch : level1_candidate_output) {
      scratch = make_scratch();
    }
    for (auto &output : production_two_card_output) {
      output.resize(shape.hero_hands);
    }
    for (std::size_t local = 0U; local < shape.opponent_hands; ++local) {
      const auto rank = (local * 37U + local / 7U) % shape.rank_count;
      const auto first_card = (local * 11U + rank * 3U) % card_count;
      auto second_card = (local * 17U + rank * 5U + 1U) % card_count;
      if (second_card == first_card) {
        second_card = (second_card + 1U) % card_count;
      }
      opponent[local] = ComboData{
          static_cast<std::uint16_t>(rank),
          static_cast<std::uint16_t>(rank * card_count + first_card),
          static_cast<std::uint16_t>(rank * card_count + second_card), 0U, 0U,
          static_cast<std::uint16_t>(local)};
      opponent_reach[local] =
          static_cast<float>(((local * 29U + 17U) % 997U) + 1U) / 997.0F;
      parent_reach[local] =
          static_cast<float>(((local * 41U + 23U) % 991U) + 1U) / 991.0F;
      action_strategy[local] =
          static_cast<float>(((local * 53U + 31U) % 983U) + 1U) / 983.0F;
      for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
        lane_reaches[lane][local] = static_cast<float>(
            ((local * (29U + lane * 12U) + 17U + lane * 101U) % 997U) + 1U) /
            997.0F;
      }
      touched[local * 2U] = opponent[local].first_by_rank;
      touched[local * 2U + 1U] = opponent[local].second_by_rank;
    }
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());

    const auto final_row = shape.rank_count * card_count;
    for (std::size_t local = 0U; local < shape.hero_hands; ++local) {
      const auto rank = (local * 43U + local / 5U) % shape.rank_count;
      const auto first_card = (local * 7U + rank * 11U) % card_count;
      auto second_card = (local * 13U + rank * 17U + 1U) % card_count;
      if (second_card == first_card) {
        second_card = (second_card + 1U) % card_count;
      }
      hero[local] = ComboData{
          static_cast<std::uint16_t>(rank),
          static_cast<std::uint16_t>(rank * card_count + first_card),
          static_cast<std::uint16_t>(rank * card_count + second_card),
          static_cast<std::uint16_t>(final_row + first_card),
          static_cast<std::uint16_t>(final_row + second_card),
          static_cast<std::uint16_t>(local % shape.opponent_hands)};
    }

    opponent_columns.resize(opponent.size());
    for (std::size_t local = 0U; local < opponent.size(); ++local) {
      opponent_columns.rank[local] = opponent[local].rank;
      opponent_columns.first_by_rank[local] = opponent[local].first_by_rank;
      opponent_columns.second_by_rank[local] = opponent[local].second_by_rank;
    }
    hero_columns.resize(hero.size());
    for (std::size_t local = 0U; local < hero.size(); ++local) {
      hero_columns.rank[local] = hero[local].rank;
      hero_columns.first_by_rank[local] = hero[local].first_by_rank;
      hero_columns.second_by_rank[local] = hero[local].second_by_rank;
      hero_columns.first_all[local] = hero[local].first_all;
      hero_columns.second_all[local] = hero[local].second_all;
      hero_columns.opponent_slot[local] =
          local % 13U == 12U ? invalid_slot : hero[local].opponent_slot;
    }

    run_baseline(baseline);
    run_candidate(candidate);
    if (baseline.output.size() != candidate.output.size()) {
      std::abort();
    }
    for (std::size_t index = 0U; index < baseline.output.size(); ++index) {
      if (std::bit_cast<std::uint32_t>(baseline.output[index]) !=
          std::bit_cast<std::uint32_t>(candidate.output[index])) {
        std::abort();
      }
    }
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      run_baseline(sequential[lane], lane_reaches[lane],
                   lane_win_payoffs[lane], lane_tie_payoffs[lane],
                   lane_loss_payoffs[lane]);
    }
    run_four_lane(four_lane);
    for (std::size_t local = 0U; local < shape.hero_hands; ++local) {
      for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
        if (std::bit_cast<std::uint32_t>(sequential[lane].output[local]) !=
            std::bit_cast<std::uint32_t>(
                four_lane.output[local * batch_lane_count + lane])) {
          std::abort();
        }
      }
    }
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      run_production_reference(production_reference[lane],
                               lane_reaches[lane],
                               production_lane_payoffs[lane]);
    }
    run_batch_accum_production_finish();
    run_batch_two_card_avx2();
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      run_production_reference(production_same_reach_reference[lane],
                               opponent_reach,
                               production_lane_payoffs[lane]);
    }
    run_same_reach_shared_aggregation();
    for (std::size_t local = 0U; local < shape.hero_hands; ++local) {
      for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
        if (std::bit_cast<std::uint32_t>(
                production_same_reach_reference[lane].output[local]) !=
            std::bit_cast<std::uint32_t>(
                production_same_reach_shared[lane].output[local])) {
          std::abort();
        }
      }
    }
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      const auto bytes = shape.hero_hands * sizeof(float);
      if (std::memcmp(production_reference[lane].output.data(),
                      production_batch_finish[lane].output.data(),
                      bytes) != 0 ||
          std::memcmp(production_reference[lane].output.data(),
                      production_two_card_output[lane].data(), bytes) != 0) {
        std::abort();
      }
    }
    validate_terminal_reach_level1();
  }

  [[nodiscard]] Scratch make_scratch() const {
    return Scratch{
        std::vector<float>(shape.rank_count),
        std::vector<float>(shape.rank_count),
        std::vector<float>(shape.rank_count * card_count),
        std::vector<float>(shape.rank_count + 1U),
        std::vector<float>((shape.rank_count + 1U) * card_count),
        std::vector<float>(shape.hero_hands),
    };
  }

  [[nodiscard]] FourLaneScratch make_four_lane_scratch() const {
    return FourLaneScratch{
        std::vector<float>(shape.rank_count * batch_lane_count),
        std::vector<float>(shape.rank_count * batch_lane_count),
        std::vector<float>(shape.rank_count * card_count * batch_lane_count),
        std::vector<float>((shape.rank_count + 1U) * batch_lane_count),
        std::vector<float>((shape.rank_count + 1U) * card_count *
                           batch_lane_count),
        std::vector<float>(shape.hero_hands * batch_lane_count),
    };
  }

  [[nodiscard]] FourLaneAccumScratch make_four_lane_accum_scratch() const {
    return FourLaneAccumScratch{
        std::vector<float>(shape.rank_count * batch_lane_count),
        std::vector<float>(shape.rank_count * card_count * batch_lane_count),
    };
  }

  void accumulate(Scratch &scratch, const std::vector<float> &reach) const {
    std::fill(scratch.totals.begin(), scratch.totals.end(), 0.0F);
    scratch.prefix[0] = 0.0F;
    std::fill_n(scratch.card_prefix.begin(), card_count, 0.0F);
    for (std::size_t local = 0U; local < opponent.size(); ++local) {
      const auto &combo = opponent[local];
      const float weight = reach[local];
      scratch.totals[combo.rank] += weight;
      scratch.by_card[combo.first_by_rank] += weight;
      scratch.by_card[combo.second_by_rank] += weight;
    }
  }

  static void build_card_prefix(Scratch &scratch, const std::size_t rank) {
    const auto source = rank * card_count;
    const auto destination = source + card_count;
    for (std::size_t card = 0U; card < card_count; ++card) {
      scratch.card_prefix[destination + card] =
          scratch.card_prefix[source + card] + scratch.by_card[source + card];
    }
  }

  void produce_output(Scratch &scratch,
                      const std::vector<float> &reach,
                      const float lane_win_payoff,
                      const float lane_tie_payoff,
                      const float lane_loss_payoff) const {
    for (std::size_t local = 0U; local < hero.size(); ++local) {
      const auto &combo = hero[local];
      const float own_reach = reach[combo.opponent_slot];
      const float invalid_lower = scratch.card_prefix[combo.first_by_rank] +
                                  scratch.card_prefix[combo.second_by_rank];
      const float invalid_tie = scratch.by_card[combo.first_by_rank] +
                                scratch.by_card[combo.second_by_rank] - own_reach;
      const float invalid_all = scratch.card_prefix[combo.first_all] +
                                scratch.card_prefix[combo.second_all] - own_reach;
      const float numerator =
          scratch.rank_base[combo.rank] +
          invalid_lower * (lane_loss_payoff - lane_win_payoff) +
          invalid_tie * (lane_loss_payoff - lane_tie_payoff) -
          invalid_all * lane_loss_payoff;
      scratch.output[local] = numerator * inverse_normalization;
    }
    for (const auto cell : touched) {
      scratch.by_card[cell] = 0.0F;
    }
  }

  void run_baseline(Scratch &scratch,
                    const std::vector<float> &reach,
                    const float lane_win_payoff,
                    const float lane_tie_payoff,
                    const float lane_loss_payoff) const {
    accumulate(scratch, reach);
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      scratch.prefix[rank + 1U] = scratch.prefix[rank] + scratch.totals[rank];
      build_card_prefix(scratch, rank);
    }
    const float total_reach = scratch.prefix[shape.rank_count];
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      scratch.rank_base[rank] =
          scratch.prefix[rank] * lane_win_payoff +
          scratch.totals[rank] * lane_tie_payoff +
          (total_reach - scratch.prefix[rank + 1U]) * lane_loss_payoff;
    }
    produce_output(scratch, reach, lane_win_payoff, lane_tie_payoff,
                   lane_loss_payoff);
  }

  void run_baseline(Scratch &scratch) const {
    run_baseline(scratch, opponent_reach, win_payoff, tie_payoff,
                 loss_payoff);
  }

  void run_candidate(Scratch &scratch) const {
    accumulate(scratch, opponent_reach);
    float total_reach = 0.0F;
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      total_reach += scratch.totals[rank];
    }
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      const float lower = scratch.prefix[rank];
      const float upper = lower + scratch.totals[rank];
      scratch.prefix[rank + 1U] = upper;
      build_card_prefix(scratch, rank);
      scratch.rank_base[rank] = lower * win_payoff +
                                scratch.totals[rank] * tie_payoff +
                                (total_reach - upper) * loss_payoff;
    }
    produce_output(scratch, opponent_reach, win_payoff, tie_payoff,
                   loss_payoff);
  }

  void run_four_lane(FourLaneScratch &scratch) const {
    const __m128 zero = _mm_setzero_ps();
    std::fill(scratch.totals.begin(), scratch.totals.end(), 0.0F);
    _mm_storeu_ps(scratch.prefix.data(), zero);
    std::fill_n(scratch.card_prefix.begin(), card_count * batch_lane_count,
                0.0F);

    for (std::size_t local = 0U; local < opponent.size(); ++local) {
      const auto &combo = opponent[local];
      const __m128 weight = _mm_set_ps(
          lane_reaches[3][local], lane_reaches[2][local],
          lane_reaches[1][local], lane_reaches[0][local]);
      const auto add_cell = [&](std::vector<float> &cells,
                                const std::size_t cell) {
        float *const destination =
            cells.data() + cell * batch_lane_count;
        _mm_storeu_ps(destination,
                      _mm_add_ps(_mm_loadu_ps(destination), weight));
      };
      add_cell(scratch.totals, combo.rank);
      add_cell(scratch.by_card, combo.first_by_rank);
      add_cell(scratch.by_card, combo.second_by_rank);
    }

    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      const float *const previous =
          scratch.prefix.data() + rank * batch_lane_count;
      float *const next =
          scratch.prefix.data() + (rank + 1U) * batch_lane_count;
      const __m128 total = _mm_loadu_ps(
          scratch.totals.data() + rank * batch_lane_count);
      _mm_storeu_ps(next, _mm_add_ps(_mm_loadu_ps(previous), total));
      for (std::size_t card = 0U; card < card_count; ++card) {
        const auto source = (rank * card_count + card) * batch_lane_count;
        const auto destination = source + card_count * batch_lane_count;
        _mm_storeu_ps(
            scratch.card_prefix.data() + destination,
            _mm_add_ps(_mm_loadu_ps(scratch.card_prefix.data() + source),
                       _mm_loadu_ps(scratch.by_card.data() + source)));
      }
    }

    const __m128 win = _mm_loadu_ps(lane_win_payoffs.data());
    const __m128 tie = _mm_loadu_ps(lane_tie_payoffs.data());
    const __m128 loss = _mm_loadu_ps(lane_loss_payoffs.data());
    const __m128 total_reach = _mm_loadu_ps(
        scratch.prefix.data() + shape.rank_count * batch_lane_count);
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      const __m128 lower = _mm_loadu_ps(
          scratch.prefix.data() + rank * batch_lane_count);
      const __m128 equal = _mm_loadu_ps(
          scratch.totals.data() + rank * batch_lane_count);
      const __m128 higher = _mm_sub_ps(
          total_reach,
          _mm_loadu_ps(scratch.prefix.data() +
                       (rank + 1U) * batch_lane_count));
      const __m128 base = _mm_add_ps(
          _mm_add_ps(_mm_mul_ps(lower, win), _mm_mul_ps(equal, tie)),
          _mm_mul_ps(higher, loss));
      _mm_storeu_ps(scratch.rank_base.data() + rank * batch_lane_count,
                    base);
    }

    const __m128 lower_coefficient = _mm_sub_ps(loss, win);
    const __m128 tie_coefficient = _mm_sub_ps(loss, tie);
    const __m128 normalization = _mm_set1_ps(inverse_normalization);
    for (std::size_t local = 0U; local < hero.size(); ++local) {
      const auto &combo = hero[local];
      const __m128 own_reach = _mm_set_ps(
          lane_reaches[3][combo.opponent_slot],
          lane_reaches[2][combo.opponent_slot],
          lane_reaches[1][combo.opponent_slot],
          lane_reaches[0][combo.opponent_slot]);
      const auto load_cell = [](const std::vector<float> &cells,
                                const std::size_t cell) {
        return _mm_loadu_ps(cells.data() + cell * batch_lane_count);
      };
      const __m128 invalid_lower = _mm_add_ps(
          load_cell(scratch.card_prefix, combo.first_by_rank),
          load_cell(scratch.card_prefix, combo.second_by_rank));
      const __m128 invalid_tie = _mm_sub_ps(
          _mm_add_ps(load_cell(scratch.by_card, combo.first_by_rank),
                     load_cell(scratch.by_card, combo.second_by_rank)),
          own_reach);
      const __m128 invalid_all = _mm_sub_ps(
          _mm_add_ps(load_cell(scratch.card_prefix, combo.first_all),
                     load_cell(scratch.card_prefix, combo.second_all)),
          own_reach);
      __m128 numerator = load_cell(scratch.rank_base, combo.rank);
      numerator = _mm_add_ps(
          numerator, _mm_mul_ps(invalid_lower, lower_coefficient));
      numerator = _mm_add_ps(
          numerator, _mm_mul_ps(invalid_tie, tie_coefficient));
      numerator = _mm_sub_ps(numerator, _mm_mul_ps(invalid_all, loss));
      _mm_storeu_ps(scratch.output.data() + local * batch_lane_count,
                    _mm_mul_ps(numerator, normalization));
    }
    for (const auto cell : touched) {
      _mm_storeu_ps(scratch.by_card.data() + cell * batch_lane_count, zero);
    }
  }

  void accumulate_production_scalar(
      Scratch &scratch, const std::vector<float> &reach) const {
    std::fill(scratch.totals.begin(), scratch.totals.end(), 0.0F);
    scratch.prefix[0] = 0.0F;
    std::fill_n(scratch.card_prefix.begin(), card_count, 0.0F);
    const auto accumulate_one = [&](const std::size_t local,
                                    const float weight) {
      scratch.totals[opponent_columns.rank[local]] += weight;
      scratch.by_card[opponent_columns.first_by_rank[local]] += weight;
      scratch.by_card[opponent_columns.second_by_rank[local]] += weight;
    };
    std::size_t local = 0U;
    for (; local + 4U <= shape.opponent_hands; local += 4U) {
      const float weight_0 = reach[local];
      const float weight_1 = reach[local + 1U];
      const float weight_2 = reach[local + 2U];
      const float weight_3 = reach[local + 3U];
      accumulate_one(local, weight_0);
      accumulate_one(local + 1U, weight_1);
      accumulate_one(local + 2U, weight_2);
      accumulate_one(local + 3U, weight_3);
    }
    for (; local < shape.opponent_hands; ++local) {
      accumulate_one(local, reach[local]);
    }
  }

  void prepare_production_prefix(Scratch &scratch) const {
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      scratch.prefix[rank + 1U] =
          scratch.prefix[rank] + scratch.totals[rank];
      const auto source = rank * card_count;
      const auto destination = source + card_count;
      std::size_t card = 0U;
      for (; card + 8U <= card_count; card += 8U) {
        _mm256_storeu_ps(
            scratch.card_prefix.data() + destination + card,
            _mm256_add_ps(
                _mm256_loadu_ps(scratch.card_prefix.data() + source + card),
                _mm256_loadu_ps(scratch.by_card.data() + source + card)));
      }
      for (; card < card_count; ++card) {
        scratch.card_prefix[destination + card] =
            scratch.card_prefix[source + card] +
            scratch.by_card[source + card];
      }
    }
  }

  void produce_production_avx2(const Scratch &prepared, Scratch &output,
                               const std::vector<float> &reach,
                               const PayoffTriple payoff) const {
    const float lane_win = static_cast<float>(payoff.win);
    const float lane_tie = static_cast<float>(payoff.tie);
    const float lane_loss = static_cast<float>(payoff.loss);
    const float total_reach = prepared.prefix[shape.rank_count];
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      output.rank_base[rank] =
          prepared.prefix[rank] * lane_win +
          prepared.totals[rank] * lane_tie +
          (total_reach - prepared.prefix[rank + 1U]) * lane_loss;
    }

    const __m256 zero = _mm256_setzero_ps();
    const __m256 loss = _mm256_set1_ps(lane_loss);
    const __m256 lower_coefficient = _mm256_set1_ps(
        static_cast<float>(payoff.loss - payoff.win));
    const __m256 tie_coefficient = _mm256_set1_ps(
        static_cast<float>(payoff.loss - payoff.tie));
    const __m256 normalization = _mm256_set1_ps(inverse_normalization);
    const float *const all_by_card =
        prepared.card_prefix.data() + shape.rank_count * card_count;
    std::size_t local = 0U;
    for (; local + 8U <= shape.hero_hands; local += 8U) {
      const auto load_indices = [local](const std::vector<std::uint16_t> &source) {
        return _mm256_cvtepu16_epi32(_mm_loadu_si128(
            reinterpret_cast<const __m128i *>(source.data() + local)));
      };
      const __m256i opponent_indices =
          load_indices(hero_columns.opponent_slot);
      const __m256i valid_slots = _mm256_cmpgt_epi32(
          _mm256_set1_epi32(static_cast<int>(invalid_slot)),
          opponent_indices);
      const __m256 own_reaches = _mm256_mask_i32gather_ps(
          zero, reach.data(), opponent_indices,
          _mm256_castsi256_ps(valid_slots), 4);
      const __m256i first_by_rank =
          load_indices(hero_columns.first_by_rank);
      const __m256i second_by_rank =
          load_indices(hero_columns.second_by_rank);
      const __m256 invalid_lower = _mm256_add_ps(
          _mm256_i32gather_ps(prepared.card_prefix.data(), first_by_rank, 4),
          _mm256_i32gather_ps(prepared.card_prefix.data(), second_by_rank, 4));
      const __m256 invalid_tie = _mm256_sub_ps(
          _mm256_add_ps(
              _mm256_i32gather_ps(prepared.by_card.data(), first_by_rank, 4),
              _mm256_i32gather_ps(prepared.by_card.data(), second_by_rank, 4)),
          own_reaches);
      const __m256i ranks = load_indices(hero_columns.rank);
      const __m256i rank_offsets =
          _mm256_mullo_epi32(ranks, _mm256_set1_epi32(36));
      const __m256i first_cards =
          _mm256_sub_epi32(first_by_rank, rank_offsets);
      const __m256i second_cards =
          _mm256_sub_epi32(second_by_rank, rank_offsets);
      const __m256 invalid_all = _mm256_sub_ps(
          _mm256_add_ps(_mm256_i32gather_ps(all_by_card, first_cards, 4),
                        _mm256_i32gather_ps(all_by_card, second_cards, 4)),
          own_reaches);
      __m256 numerator =
          _mm256_i32gather_ps(output.rank_base.data(), ranks, 4);
      numerator = _mm256_add_ps(
          numerator, _mm256_mul_ps(invalid_lower, lower_coefficient));
      numerator = _mm256_add_ps(
          numerator, _mm256_mul_ps(invalid_tie, tie_coefficient));
      numerator =
          _mm256_sub_ps(numerator, _mm256_mul_ps(invalid_all, loss));
      _mm256_storeu_ps(output.output.data() + local,
                       _mm256_mul_ps(numerator, normalization));
    }
    for (; local < shape.hero_hands; ++local) {
      const auto opponent_slot = hero_columns.opponent_slot[local];
      const float own_reach = opponent_slot == invalid_slot
                                  ? 0.0F
                                  : reach[opponent_slot];
      const float invalid_lower =
          prepared.card_prefix[hero_columns.first_by_rank[local]] +
          prepared.card_prefix[hero_columns.second_by_rank[local]];
      const float invalid_tie =
          prepared.by_card[hero_columns.first_by_rank[local]] +
          prepared.by_card[hero_columns.second_by_rank[local]] - own_reach;
      const float invalid_all =
          prepared.card_prefix[hero_columns.first_all[local]] +
          prepared.card_prefix[hero_columns.second_all[local]] - own_reach;
      const float numerator =
          output.rank_base[hero_columns.rank[local]] +
          invalid_lower * static_cast<float>(payoff.loss - payoff.win) +
          invalid_tie * static_cast<float>(payoff.loss - payoff.tie) -
          invalid_all * lane_loss;
      output.output[local] = numerator * inverse_normalization;
    }
  }

  void finish_production_avx2(Scratch &scratch,
                              const std::vector<float> &reach,
                              const PayoffTriple payoff) const {
    prepare_production_prefix(scratch);
    produce_production_avx2(scratch, scratch, reach, payoff);
    for (const auto cell : touched) {
      scratch.by_card[cell] = 0.0F;
    }
  }

  void run_same_reach_shared_aggregation() {
    accumulate_production_scalar(production_shared_reach, opponent_reach);
    prepare_production_prefix(production_shared_reach);
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      produce_production_avx2(production_shared_reach,
                              production_same_reach_shared[lane],
                              opponent_reach, production_lane_payoffs[lane]);
    }
    for (const auto cell : touched) {
      production_shared_reach.by_card[cell] = 0.0F;
    }
  }

  void run_production_reference(Scratch &scratch,
                                const std::vector<float> &reach,
                                const PayoffTriple payoff) const {
    accumulate_production_scalar(scratch, reach);
    finish_production_avx2(scratch, reach, payoff);
  }

  void materialize_level1_child(std::vector<float> &child) const {
    std::size_t local = 0U;
    for (; local + 4U <= shape.opponent_hands; local += 4U) {
      const __m256d parents =
          _mm256_cvtps_pd(_mm_loadu_ps(parent_reach.data() + local));
      const __m256d strategies =
          _mm256_cvtps_pd(_mm_loadu_ps(action_strategy.data() + local));
      _mm_storeu_ps(child.data() + local,
                    _mm256_cvtpd_ps(_mm256_mul_pd(parents, strategies)));
    }
    for (; local < shape.opponent_hands; ++local) {
      child[local] = static_cast<float>(
          static_cast<double>(parent_reach[local]) *
          static_cast<double>(action_strategy[local]));
    }
  }

  void materialize_level1_view(Scratch &summary,
                               std::vector<float> &child) const {
    std::fill(summary.totals.begin(), summary.totals.end(), 0.0F);
    summary.prefix[0] = 0.0F;
    std::fill_n(summary.card_prefix.begin(), card_count, 0.0F);
    const auto accumulate_one = [&](const std::size_t local) {
      const float weight = child[local];
      summary.totals[opponent_columns.rank[local]] += weight;
      summary.by_card[opponent_columns.first_by_rank[local]] += weight;
      summary.by_card[opponent_columns.second_by_rank[local]] += weight;
    };
    std::size_t local = 0U;
    for (; local + 4U <= shape.opponent_hands; local += 4U) {
      const __m256d parents =
          _mm256_cvtps_pd(_mm_loadu_ps(parent_reach.data() + local));
      const __m256d strategies =
          _mm256_cvtps_pd(_mm_loadu_ps(action_strategy.data() + local));
      _mm_storeu_ps(child.data() + local,
                    _mm256_cvtpd_ps(_mm256_mul_pd(parents, strategies)));
      accumulate_one(local);
      accumulate_one(local + 1U);
      accumulate_one(local + 2U);
      accumulate_one(local + 3U);
    }
    for (; local < shape.opponent_hands; ++local) {
      child[local] = static_cast<float>(
          static_cast<double>(parent_reach[local]) *
          static_cast<double>(action_strategy[local]));
      accumulate_one(local);
    }
    prepare_production_prefix(summary);
  }

  void reset_level1_summary(Scratch &summary) const {
    for (const auto cell : touched) {
      summary.by_card[cell] = 0.0F;
    }
  }

  void run_terminal_reach_reference(const std::size_t query_count) {
    materialize_level1_child(level1_reference_child);
    for (std::size_t query = 0U; query < query_count; ++query) {
      run_production_reference(level1_reference_output[query],
                               level1_reference_child,
                               production_lane_payoffs[query]);
    }
  }

  void run_terminal_reach_candidate(const std::size_t query_count) {
    materialize_level1_view(level1_candidate_summary,
                            level1_candidate_child);
    for (std::size_t query = 0U; query < query_count; ++query) {
      produce_production_avx2(level1_candidate_summary,
                              level1_candidate_output[query],
                              level1_candidate_child,
                              production_lane_payoffs[query]);
    }
    reset_level1_summary(level1_candidate_summary);
  }

  void validate_terminal_reach_level1() {
    materialize_level1_child(level1_reference_child);
    accumulate_production_scalar(level1_reference_summary,
                                 level1_reference_child);
    prepare_production_prefix(level1_reference_summary);
    materialize_level1_view(level1_candidate_summary,
                            level1_candidate_child);

    const auto same_bits = [](const std::vector<float> &left,
                              const std::vector<float> &right) {
      return left.size() == right.size() &&
             std::memcmp(left.data(), right.data(),
                         left.size() * sizeof(float)) == 0;
    };
    if (!same_bits(level1_reference_child, level1_candidate_child) ||
        !same_bits(level1_reference_summary.totals,
                   level1_candidate_summary.totals) ||
        !same_bits(level1_reference_summary.by_card,
                   level1_candidate_summary.by_card) ||
        !same_bits(level1_reference_summary.prefix,
                   level1_candidate_summary.prefix) ||
        !same_bits(level1_reference_summary.card_prefix,
                   level1_candidate_summary.card_prefix)) {
      std::abort();
    }
    for (std::size_t query = 0U; query < batch_lane_count; ++query) {
      produce_production_avx2(level1_reference_summary,
                              level1_reference_output[query],
                              level1_reference_child,
                              production_lane_payoffs[query]);
      produce_production_avx2(level1_candidate_summary,
                              level1_candidate_output[query],
                              level1_candidate_child,
                              production_lane_payoffs[query]);
      if (!same_bits(level1_reference_output[query].rank_base,
                     level1_candidate_output[query].rank_base) ||
          !same_bits(level1_reference_output[query].output,
                     level1_candidate_output[query].output)) {
        std::abort();
      }
    }
    reset_level1_summary(level1_reference_summary);
    reset_level1_summary(level1_candidate_summary);
  }

  void accumulate_four_interleaved(std::vector<float> &totals,
                                   std::vector<float> &by_card) const {
    std::fill(totals.begin(), totals.end(), 0.0F);
    for (std::size_t local = 0U; local < shape.opponent_hands; ++local) {
      const __m128 weight = _mm_set_ps(
          lane_reaches[3][local], lane_reaches[2][local],
          lane_reaches[1][local], lane_reaches[0][local]);
      const auto add_cell = [&](std::vector<float> &cells,
                                const std::size_t cell) {
        float *const destination =
            cells.data() + cell * batch_lane_count;
        _mm_storeu_ps(destination,
                      _mm_add_ps(_mm_loadu_ps(destination), weight));
      };
      add_cell(totals, opponent_columns.rank[local]);
      add_cell(by_card, opponent_columns.first_by_rank[local]);
      add_cell(by_card, opponent_columns.second_by_rank[local]);
    }
  }

  void run_batch_accum_production_finish() {
    accumulate_four_interleaved(production_batch_accum.totals,
                                production_batch_accum.by_card);
    alignas(16) float lanes[batch_lane_count];
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      _mm_store_ps(lanes, _mm_loadu_ps(
          production_batch_accum.totals.data() + rank * batch_lane_count));
      for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
        production_batch_finish[lane].totals[rank] = lanes[lane];
      }
    }
    for (const auto cell : touched) {
      _mm_store_ps(lanes, _mm_loadu_ps(
          production_batch_accum.by_card.data() +
          static_cast<std::size_t>(cell) * batch_lane_count));
      for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
        production_batch_finish[lane].by_card[cell] = lanes[lane];
      }
      _mm_storeu_ps(production_batch_accum.by_card.data() +
                        static_cast<std::size_t>(cell) * batch_lane_count,
                    _mm_setzero_ps());
    }
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      production_batch_finish[lane].prefix[0] = 0.0F;
      std::fill_n(production_batch_finish[lane].card_prefix.begin(),
                  card_count, 0.0F);
      finish_production_avx2(production_batch_finish[lane],
                             lane_reaches[lane],
                             production_lane_payoffs[lane]);
    }
  }

  void run_batch_two_card_avx2() {
    auto &scratch = production_two_card;
    accumulate_four_interleaved(scratch.totals, scratch.by_card);
    _mm_storeu_ps(scratch.prefix.data(), _mm_setzero_ps());
    std::fill_n(scratch.card_prefix.begin(),
                card_count * batch_lane_count, 0.0F);
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      const auto prefix_source = rank * batch_lane_count;
      const auto prefix_destination = (rank + 1U) * batch_lane_count;
      _mm_storeu_ps(
          scratch.prefix.data() + prefix_destination,
          _mm_add_ps(_mm_loadu_ps(scratch.prefix.data() + prefix_source),
                     _mm_loadu_ps(scratch.totals.data() + prefix_source)));
      for (std::size_t card = 0U; card < card_count; card += 2U) {
        const auto source =
            (rank * card_count + card) * batch_lane_count;
        const auto destination = source + card_count * batch_lane_count;
        _mm256_storeu_ps(
            scratch.card_prefix.data() + destination,
            _mm256_add_ps(
                _mm256_loadu_ps(scratch.card_prefix.data() + source),
                _mm256_loadu_ps(scratch.by_card.data() + source)));
      }
    }

    const auto payoff_vector = [](const auto selector) {
      return _mm_set_ps(
          selector(production_lane_payoffs[3]),
          selector(production_lane_payoffs[2]),
          selector(production_lane_payoffs[1]),
          selector(production_lane_payoffs[0]));
    };
    const __m128 win4 = payoff_vector([](const PayoffTriple value) {
      return static_cast<float>(value.win);
    });
    const __m128 tie4 = payoff_vector([](const PayoffTriple value) {
      return static_cast<float>(value.tie);
    });
    const __m128 loss4 = payoff_vector([](const PayoffTriple value) {
      return static_cast<float>(value.loss);
    });
    const __m128 lower4 = payoff_vector([](const PayoffTriple value) {
      return static_cast<float>(value.loss - value.win);
    });
    const __m128 tie_coefficient4 = payoff_vector([](const PayoffTriple value) {
      return static_cast<float>(value.loss - value.tie);
    });
    const __m128 total4 = _mm_loadu_ps(
        scratch.prefix.data() + shape.rank_count * batch_lane_count);
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      const __m128 lower = _mm_loadu_ps(
          scratch.prefix.data() + rank * batch_lane_count);
      const __m128 equal = _mm_loadu_ps(
          scratch.totals.data() + rank * batch_lane_count);
      const __m128 higher = _mm_sub_ps(
          total4, _mm_loadu_ps(scratch.prefix.data() +
                               (rank + 1U) * batch_lane_count));
      const __m128 base = _mm_add_ps(
          _mm_add_ps(_mm_mul_ps(lower, win4), _mm_mul_ps(equal, tie4)),
          _mm_mul_ps(higher, loss4));
      _mm_storeu_ps(scratch.rank_base.data() + rank * batch_lane_count,
                    base);
    }

    const auto duplicate = [](const __m128 value) {
      return _mm256_insertf128_ps(_mm256_castps128_ps256(value), value, 1);
    };
    const __m256 loss8 = duplicate(loss4);
    const __m256 lower8 = duplicate(lower4);
    const __m256 tie_coefficient8 = duplicate(tie_coefficient4);
    const __m256 normalization8 = _mm256_set1_ps(inverse_normalization);
    const auto load_two_cells = [](const std::vector<float> &cells,
                                   const std::size_t first,
                                   const std::size_t second) {
      return _mm256_insertf128_ps(
          _mm256_castps128_ps256(
              _mm_loadu_ps(cells.data() + first * batch_lane_count)),
          _mm_loadu_ps(cells.data() + second * batch_lane_count), 1);
    };
    const auto own_reach4 = [&](const std::size_t local) {
      const auto slot = hero_columns.opponent_slot[local];
      if (slot == invalid_slot) {
        return _mm_setzero_ps();
      }
      return _mm_set_ps(lane_reaches[3][slot], lane_reaches[2][slot],
                        lane_reaches[1][slot], lane_reaches[0][slot]);
    };
    std::size_t local = 0U;
    alignas(32) float result_lanes[8];
    for (; local + 2U <= shape.hero_hands; local += 2U) {
      const __m256 own_reaches = _mm256_insertf128_ps(
          _mm256_castps128_ps256(own_reach4(local)),
          own_reach4(local + 1U), 1);
      const __m256 invalid_lower = _mm256_add_ps(
          load_two_cells(scratch.card_prefix,
                         hero_columns.first_by_rank[local],
                         hero_columns.first_by_rank[local + 1U]),
          load_two_cells(scratch.card_prefix,
                         hero_columns.second_by_rank[local],
                         hero_columns.second_by_rank[local + 1U]));
      const __m256 invalid_tie = _mm256_sub_ps(
          _mm256_add_ps(
              load_two_cells(scratch.by_card,
                             hero_columns.first_by_rank[local],
                             hero_columns.first_by_rank[local + 1U]),
              load_two_cells(scratch.by_card,
                             hero_columns.second_by_rank[local],
                             hero_columns.second_by_rank[local + 1U])),
          own_reaches);
      const __m256 invalid_all = _mm256_sub_ps(
          _mm256_add_ps(
              load_two_cells(scratch.card_prefix,
                             hero_columns.first_all[local],
                             hero_columns.first_all[local + 1U]),
              load_two_cells(scratch.card_prefix,
                             hero_columns.second_all[local],
                             hero_columns.second_all[local + 1U])),
          own_reaches);
      __m256 numerator = load_two_cells(
          scratch.rank_base, hero_columns.rank[local],
          hero_columns.rank[local + 1U]);
      numerator = _mm256_add_ps(
          numerator, _mm256_mul_ps(invalid_lower, lower8));
      numerator = _mm256_add_ps(
          numerator, _mm256_mul_ps(invalid_tie, tie_coefficient8));
      numerator =
          _mm256_sub_ps(numerator, _mm256_mul_ps(invalid_all, loss8));
      _mm256_store_ps(result_lanes,
                      _mm256_mul_ps(numerator, normalization8));
      for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
        production_two_card_output[lane][local] = result_lanes[lane];
        production_two_card_output[lane][local + 1U] =
            result_lanes[batch_lane_count + lane];
      }
    }
    if (local < shape.hero_hands) {
      const __m128 own_reaches = own_reach4(local);
      const auto load_cell = [](const std::vector<float> &cells,
                                const std::size_t cell) {
        return _mm_loadu_ps(cells.data() + cell * batch_lane_count);
      };
      const __m128 invalid_lower = _mm_add_ps(
          load_cell(scratch.card_prefix,
                    hero_columns.first_by_rank[local]),
          load_cell(scratch.card_prefix,
                    hero_columns.second_by_rank[local]));
      const __m128 invalid_tie = _mm_sub_ps(
          _mm_add_ps(load_cell(scratch.by_card,
                               hero_columns.first_by_rank[local]),
                     load_cell(scratch.by_card,
                               hero_columns.second_by_rank[local])),
          own_reaches);
      const __m128 invalid_all = _mm_sub_ps(
          _mm_add_ps(load_cell(scratch.card_prefix,
                               hero_columns.first_all[local]),
                     load_cell(scratch.card_prefix,
                               hero_columns.second_all[local])),
          own_reaches);
      __m128 numerator =
          load_cell(scratch.rank_base, hero_columns.rank[local]);
      numerator = _mm_add_ps(numerator,
                             _mm_mul_ps(invalid_lower, lower4));
      numerator = _mm_add_ps(
          numerator, _mm_mul_ps(invalid_tie, tie_coefficient4));
      numerator =
          _mm_sub_ps(numerator, _mm_mul_ps(invalid_all, loss4));
      alignas(16) float tail[4];
      _mm_store_ps(tail, _mm_mul_ps(numerator, _mm_set1_ps(inverse_normalization)));
      for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
        production_two_card_output[lane][local] = tail[lane];
      }
    }
    for (const auto cell : touched) {
      _mm_storeu_ps(scratch.by_card.data() +
                        static_cast<std::size_t>(cell) * batch_lane_count,
                    _mm_setzero_ps());
    }
  }

  WorkloadShape shape;
  std::vector<ComboData> opponent;
  std::vector<ComboData> hero;
  ComboColumns opponent_columns;
  ComboColumns hero_columns;
  std::vector<float> opponent_reach;
  std::vector<float> parent_reach;
  std::vector<float> action_strategy;
  std::vector<float> level1_reference_child;
  std::vector<float> level1_candidate_child;
  std::vector<std::uint16_t> touched;
  Scratch baseline;
  Scratch candidate;
  std::array<std::vector<float>, batch_lane_count> lane_reaches;
  std::array<Scratch, batch_lane_count> sequential;
  FourLaneScratch four_lane;
  std::array<Scratch, batch_lane_count> production_reference;
  std::array<Scratch, batch_lane_count> production_batch_finish;
  std::array<Scratch, batch_lane_count> production_same_reach_reference;
  std::array<Scratch, batch_lane_count> production_same_reach_shared;
  Scratch production_shared_reach;
  Scratch level1_reference_summary;
  Scratch level1_candidate_summary;
  std::array<Scratch, batch_lane_count> level1_reference_output;
  std::array<Scratch, batch_lane_count> level1_candidate_output;
  std::array<std::vector<float>, batch_lane_count>
      production_two_card_output;
  FourLaneAccumScratch production_batch_accum;
  FourLaneScratch production_two_card;
};

Fixture &fixture(const std::size_t workload) {
  static Fixture small(workload_shapes[0]);
  static Fixture medium(workload_shapes[1]);
  static Fixture large(workload_shapes[2]);
  static Fixture tst_p0(workload_shapes[3]);
  static Fixture tst_p1(workload_shapes[4]);
  Fixture *const fixtures[] = {&small, &medium, &large, &tst_p0, &tst_p1};
  return *fixtures[workload];
}

void publish_metrics(benchmark::State &state, const Fixture &data,
                     const bool candidate) {
  const auto showdowns = state.iterations();
  state.SetItemsProcessed(showdowns);
  state.counters["showdowns_per_second"] =
      benchmark::Counter(static_cast<double>(showdowns), benchmark::Counter::kIsRate);
  state.counters["rank_cells"] = static_cast<double>(data.shape.rank_count);
  state.counters["hero_hands"] = static_cast<double>(data.shape.hero_hands);
  state.counters["opponent_hands"] = static_cast<double>(data.shape.opponent_hands);
  state.counters["touched_rank_card_cells"] = static_cast<double>(data.touched.size());
  const auto common_bytes =
      (data.shape.opponent_hands * 4U + data.shape.hero_hands * 13U +
       data.shape.rank_count * card_count * 3U + data.touched.size()) * sizeof(float);
  const auto rank_bytes = candidate ? data.shape.rank_count * 4U * sizeof(float)
                                    : data.shape.rank_count * 6U * sizeof(float);
  state.counters["estimated_bytes_per_showdown"] =
      static_cast<double>(common_bytes + rank_bytes);
}

void BM_ShowdownBaseline(benchmark::State &state, const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_baseline(data.baseline);
    benchmark::DoNotOptimize(data.baseline.output.data());
    benchmark::ClobberMemory();
  }
  publish_metrics(state, data, false);
}

void BM_ShowdownFusedRankBase(benchmark::State &state,
                              const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_candidate(data.candidate);
    benchmark::DoNotOptimize(data.candidate.output.data());
    benchmark::ClobberMemory();
  }
  publish_metrics(state, data, true);
}

void publish_four_lane_metrics(benchmark::State &state, const Fixture &data) {
  const auto showdowns = state.iterations() * batch_lane_count;
  state.SetItemsProcessed(showdowns);
  state.counters["showdowns_per_second"] = benchmark::Counter(
      static_cast<double>(showdowns), benchmark::Counter::kIsRate);
  state.counters["batch_lanes"] = static_cast<double>(batch_lane_count);
  state.counters["rank_cells"] = static_cast<double>(data.shape.rank_count);
  state.counters["hero_hands"] = static_cast<double>(data.shape.hero_hands);
  state.counters["opponent_hands"] =
      static_cast<double>(data.shape.opponent_hands);
}

void BM_ShowdownFourSequential(benchmark::State &state,
                               const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      data.run_baseline(data.sequential[lane], data.lane_reaches[lane],
                        lane_win_payoffs[lane], lane_tie_payoffs[lane],
                        lane_loss_payoffs[lane]);
      benchmark::DoNotOptimize(data.sequential[lane].output.data());
    }
    benchmark::ClobberMemory();
  }
  publish_four_lane_metrics(state, data);
}

void BM_ShowdownFourLaneBatch(benchmark::State &state,
                              const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_four_lane(data.four_lane);
    benchmark::DoNotOptimize(data.four_lane.output.data());
    benchmark::ClobberMemory();
  }
  publish_four_lane_metrics(state, data);
}

void BM_ShowdownFourProductionSequential(benchmark::State &state,
                                         const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      data.run_production_reference(data.production_reference[lane],
                                    data.lane_reaches[lane],
                                    production_lane_payoffs[lane]);
      benchmark::DoNotOptimize(data.production_reference[lane].output.data());
    }
    benchmark::ClobberMemory();
  }
  publish_four_lane_metrics(state, data);
}

void BM_ShowdownFourSameReachSequential(benchmark::State &state,
                                        const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      data.run_production_reference(
          data.production_same_reach_reference[lane], data.opponent_reach,
          production_lane_payoffs[lane]);
      benchmark::DoNotOptimize(
          data.production_same_reach_reference[lane].output.data());
    }
    benchmark::ClobberMemory();
  }
  publish_four_lane_metrics(state, data);
}

void BM_ShowdownFourSameReachSharedAggregation(
    benchmark::State &state, const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_same_reach_shared_aggregation();
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      benchmark::DoNotOptimize(
          data.production_same_reach_shared[lane].output.data());
    }
    benchmark::ClobberMemory();
  }
  publish_four_lane_metrics(state, data);
}

void BM_ShowdownFourBatchAccumProductionFinish(
    benchmark::State &state, const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_batch_accum_production_finish();
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      benchmark::DoNotOptimize(
          data.production_batch_finish[lane].output.data());
    }
    benchmark::ClobberMemory();
  }
  publish_four_lane_metrics(state, data);
}

void BM_ShowdownFourBatchTwoCardAvx2(benchmark::State &state,
                                     const std::size_t workload) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_batch_two_card_avx2();
    for (std::size_t lane = 0U; lane < batch_lane_count; ++lane) {
      benchmark::DoNotOptimize(data.production_two_card_output[lane].data());
    }
    benchmark::ClobberMemory();
  }
  publish_four_lane_metrics(state, data);
}

void publish_terminal_reach_metrics(benchmark::State &state,
                                    const Fixture &data,
                                    const std::size_t query_count) {
  const auto terminal_queries = state.iterations() * query_count;
  state.SetItemsProcessed(terminal_queries);
  state.counters["terminal_queries_per_second"] = benchmark::Counter(
      static_cast<double>(terminal_queries), benchmark::Counter::kIsRate);
  state.counters["queries_per_reach"] = static_cast<double>(query_count);
  state.counters["rank_cells"] = static_cast<double>(data.shape.rank_count);
  state.counters["hero_hands"] = static_cast<double>(data.shape.hero_hands);
  state.counters["opponent_hands"] =
      static_cast<double>(data.shape.opponent_hands);
  state.counters["touched_rank_card_cells"] =
      static_cast<double>(data.touched.size());
  const auto summary_floats =
      data.shape.rank_count + data.shape.rank_count * card_count +
      (data.shape.rank_count + 1U) +
      (data.shape.rank_count + 1U) * card_count;
  state.counters["view_incremental_bytes"] =
      static_cast<double>(summary_floats * sizeof(float));
}

void BM_TerminalReachReference(benchmark::State &state,
                               const std::size_t workload,
                               const std::size_t query_count) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_terminal_reach_reference(query_count);
    benchmark::DoNotOptimize(data.level1_reference_child.data());
    for (std::size_t query = 0U; query < query_count; ++query) {
      benchmark::DoNotOptimize(
          data.level1_reference_output[query].output.data());
    }
    benchmark::ClobberMemory();
  }
  publish_terminal_reach_metrics(state, data, query_count);
}

void BM_TerminalReachView(benchmark::State &state,
                          const std::size_t workload,
                          const std::size_t query_count) {
  auto &data = fixture(workload);
  for (auto _ : state) {
    static_cast<void>(_);
    data.run_terminal_reach_candidate(query_count);
    benchmark::DoNotOptimize(data.level1_candidate_child.data());
    for (std::size_t query = 0U; query < query_count; ++query) {
      benchmark::DoNotOptimize(
          data.level1_candidate_output[query].output.data());
    }
    benchmark::ClobberMemory();
  }
  publish_terminal_reach_metrics(state, data, query_count);
}

BENCHMARK_CAPTURE(BM_ShowdownBaseline, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownBaseline, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownBaseline, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFusedRankBase, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFusedRankBase, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFusedRankBase, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFourSequential, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFourSequential, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFourSequential, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFourLaneBatch, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFourLaneBatch, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFourLaneBatch, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFourProductionSequential, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFourProductionSequential, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFourProductionSequential, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFourSameReachSequential, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFourSameReachSequential, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFourSameReachSequential, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFourSameReachSharedAggregation, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFourSameReachSharedAggregation, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFourSameReachSharedAggregation, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFourBatchAccumProductionFinish, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFourBatchAccumProductionFinish, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFourBatchAccumProductionFinish, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFourBatchTwoCardAvx2, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFourBatchTwoCardAvx2, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFourBatchTwoCardAvx2, large, 2U);
BENCHMARK_CAPTURE(BM_TerminalReachReference, tst_p0_q1, 3U, 1U);
BENCHMARK_CAPTURE(BM_TerminalReachView, tst_p0_q1, 3U, 1U);
BENCHMARK_CAPTURE(BM_TerminalReachReference, tst_p0_q4, 3U, 4U);
BENCHMARK_CAPTURE(BM_TerminalReachView, tst_p0_q4, 3U, 4U);
BENCHMARK_CAPTURE(BM_TerminalReachReference, tst_p1_q1, 4U, 1U);
BENCHMARK_CAPTURE(BM_TerminalReachView, tst_p1_q1, 4U, 1U);
BENCHMARK_CAPTURE(BM_TerminalReachReference, tst_p1_q4, 4U, 4U);
BENCHMARK_CAPTURE(BM_TerminalReachView, tst_p1_q4, 4U, 4U);

} // namespace
