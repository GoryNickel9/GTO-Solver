#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <immintrin.h>
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
};

struct ComboData {
  std::uint16_t rank{};
  std::uint16_t first_by_rank{};
  std::uint16_t second_by_rank{};
  std::uint16_t first_all{};
  std::uint16_t second_all{};
  std::uint16_t opponent_slot{};
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

struct Fixture {
  explicit Fixture(const WorkloadShape shape)
      : shape(shape), opponent(shape.opponent_hands), hero(shape.hero_hands),
        opponent_reach(shape.opponent_hands),
        touched(shape.opponent_hands * 2U), baseline(make_scratch()),
        candidate(make_scratch()), four_lane(make_four_lane_scratch()) {
    for (auto &reach : lane_reaches) {
      reach.resize(shape.opponent_hands);
    }
    for (auto &scratch : sequential) {
      scratch = make_scratch();
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

  WorkloadShape shape;
  std::vector<ComboData> opponent;
  std::vector<ComboData> hero;
  std::vector<float> opponent_reach;
  std::vector<std::uint16_t> touched;
  Scratch baseline;
  Scratch candidate;
  std::array<std::vector<float>, batch_lane_count> lane_reaches;
  std::array<Scratch, batch_lane_count> sequential;
  FourLaneScratch four_lane;
};

Fixture &fixture(const std::size_t workload) {
  static Fixture small(workload_shapes[0]);
  static Fixture medium(workload_shapes[1]);
  static Fixture large(workload_shapes[2]);
  Fixture *const fixtures[] = {&small, &medium, &large};
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

} // namespace
