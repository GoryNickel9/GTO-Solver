#include <benchmark/benchmark.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t card_count = 36U;
constexpr float win_payoff = 10.25F;
constexpr float tie_payoff = -0.5F;
constexpr float loss_payoff = -11.25F;
constexpr float inverse_normalization = 0.03125F;

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

struct Fixture {
  explicit Fixture(const WorkloadShape shape)
      : shape(shape), opponent(shape.opponent_hands), hero(shape.hero_hands),
        opponent_reach(shape.opponent_hands),
        touched(shape.opponent_hands * 2U), baseline(make_scratch()),
        candidate(make_scratch()) {
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

  void accumulate(Scratch &scratch) const {
    std::fill(scratch.totals.begin(), scratch.totals.end(), 0.0F);
    scratch.prefix[0] = 0.0F;
    std::fill_n(scratch.card_prefix.begin(), card_count, 0.0F);
    for (std::size_t local = 0U; local < opponent.size(); ++local) {
      const auto &combo = opponent[local];
      const float weight = opponent_reach[local];
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

  void produce_output(Scratch &scratch) const {
    for (std::size_t local = 0U; local < hero.size(); ++local) {
      const auto &combo = hero[local];
      const float own_reach = opponent_reach[combo.opponent_slot];
      const float invalid_lower = scratch.card_prefix[combo.first_by_rank] +
                                  scratch.card_prefix[combo.second_by_rank];
      const float invalid_tie = scratch.by_card[combo.first_by_rank] +
                                scratch.by_card[combo.second_by_rank] - own_reach;
      const float invalid_all = scratch.card_prefix[combo.first_all] +
                                scratch.card_prefix[combo.second_all] - own_reach;
      const float numerator =
          scratch.rank_base[combo.rank] +
          invalid_lower * (loss_payoff - win_payoff) +
          invalid_tie * (loss_payoff - tie_payoff) - invalid_all * loss_payoff;
      scratch.output[local] = numerator * inverse_normalization;
    }
    for (const auto cell : touched) {
      scratch.by_card[cell] = 0.0F;
    }
  }

  void run_baseline(Scratch &scratch) const {
    accumulate(scratch);
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      scratch.prefix[rank + 1U] = scratch.prefix[rank] + scratch.totals[rank];
      build_card_prefix(scratch, rank);
    }
    const float total_reach = scratch.prefix[shape.rank_count];
    for (std::size_t rank = 0U; rank < shape.rank_count; ++rank) {
      scratch.rank_base[rank] =
          scratch.prefix[rank] * win_payoff + scratch.totals[rank] * tie_payoff +
          (total_reach - scratch.prefix[rank + 1U]) * loss_payoff;
    }
    produce_output(scratch);
  }

  void run_candidate(Scratch &scratch) const {
    accumulate(scratch);
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
    produce_output(scratch);
  }

  WorkloadShape shape;
  std::vector<ComboData> opponent;
  std::vector<ComboData> hero;
  std::vector<float> opponent_reach;
  std::vector<std::uint16_t> touched;
  Scratch baseline;
  Scratch candidate;
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

BENCHMARK_CAPTURE(BM_ShowdownBaseline, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownBaseline, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownBaseline, large, 2U);
BENCHMARK_CAPTURE(BM_ShowdownFusedRankBase, small, 0U);
BENCHMARK_CAPTURE(BM_ShowdownFusedRankBase, medium, 1U);
BENCHMARK_CAPTURE(BM_ShowdownFusedRankBase, large, 2U);

} // namespace
