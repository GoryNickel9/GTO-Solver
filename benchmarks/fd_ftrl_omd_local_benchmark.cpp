#include <algorithm>
#include <array>
#include <benchmark/benchmark.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace {

constexpr std::size_t sample_count = 4096U;

template <std::size_t ActionCount> struct Sample {
  std::array<float, ActionCount> loss{};
  std::array<float, ActionCount> center{};
  double reach{};
  double lambda{};
};

template <std::size_t ActionCount>
const std::vector<Sample<ActionCount>> &samples() {
  static const auto data = [] {
    std::vector<Sample<ActionCount>> result(sample_count);
    std::uint32_t state = 0x9e3779b9U + static_cast<std::uint32_t>(ActionCount);
    const auto next = [&state]() {
      state ^= state << 13U;
      state ^= state >> 17U;
      state ^= state << 5U;
      return state;
    };
    for (auto &sample : result) {
      sample.reach = 0.01 + static_cast<double>(next() % 9900U) / 10'000.0;
      sample.lambda = 0.05 + static_cast<double>(next() % 20'000U) / 10'000.0;
      for (std::size_t action = 0U; action < ActionCount; ++action) {
        sample.loss[action] =
            static_cast<float>(static_cast<std::int32_t>(next() % 20'001U) - 10'000) / 4096.0F;
        sample.center[action] = static_cast<float>(next() % 16'384U) / 4096.0F;
      }
    }
    return result;
  }();
  return data;
}

template <std::size_t ActionCount>
double solve_direct(const std::array<double, ActionCount> &thresholds, const double lambda) {
  std::array<bool, ActionCount> selected{};
  double prefix = 0.0;
  double square_prefix = 0.0;
  for (std::size_t active = 1U; active <= ActionCount; ++active) {
    std::size_t smallest_index = ActionCount;
    double smallest = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < ActionCount; ++index) {
      if (!selected[index] && thresholds[index] < smallest) {
        smallest = thresholds[index];
        smallest_index = index;
      }
    }
    selected[smallest_index] = true;
    prefix += smallest;
    square_prefix += smallest * smallest;
    double next = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < ActionCount; ++index) {
      if (!selected[index]) {
        next = std::min(next, thresholds[index]);
      }
    }
    const double count = static_cast<double>(active);
    const double discriminant =
        std::max(0.0, prefix * prefix - count * (square_prefix - lambda));
    const double alpha = (prefix + std::sqrt(discriminant)) / count;
    if (active == ActionCount || alpha <= next) {
      return alpha;
    }
  }
  return 0.0;
}

template <std::size_t ActionCount>
double solve_sorted(std::array<double, ActionCount> thresholds, const double lambda) {
  std::ranges::sort(thresholds);
  double prefix = 0.0;
  double square_prefix = 0.0;
  for (std::size_t active = 1U; active <= ActionCount; ++active) {
    const double threshold = thresholds[active - 1U];
    prefix += threshold;
    square_prefix += threshold * threshold;
    const double count = static_cast<double>(active);
    const double discriminant =
        std::max(0.0, prefix * prefix - count * (square_prefix - lambda));
    const double alpha = (prefix + std::sqrt(discriminant)) / count;
    if (active == ActionCount || alpha <= thresholds[active]) {
      return alpha;
    }
  }
  return 0.0;
}

template <std::size_t ActionCount>
double solve_bisection(const std::array<double, ActionCount> &thresholds, const double lambda) {
  const auto [minimum, maximum] = std::ranges::minmax_element(thresholds);
  double lower = *minimum;
  double upper = *maximum + std::sqrt(lambda);
  for (std::size_t iteration = 0U; iteration < 32U; ++iteration) {
    const double middle = 0.5 * (lower + upper);
    double norm_squared = 0.0;
    for (const double threshold : thresholds) {
      const double value = std::max(0.0, middle - threshold);
      norm_squared += value * value;
    }
    if (norm_squared < lambda) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  return 0.5 * (lower + upper);
}

template <std::size_t ActionCount>
void store_policy(const std::array<double, ActionCount> &weights,
                  std::array<float, ActionCount> &output) {
  double sum = 0.0;
  for (const double weight : weights) {
    sum += std::max(0.0, weight);
  }
  const double fallback = 1.0 / static_cast<double>(ActionCount);
  for (std::size_t action = 0U; action < ActionCount; ++action) {
    output[action] = static_cast<float>(sum > 0.0 ? std::max(0.0, weights[action]) / sum
                                                 : fallback);
  }
}

template <std::size_t ActionCount> void bm_regret_matching(benchmark::State &state) {
  const auto &data = samples<ActionCount>();
  std::array<float, ActionCount> output{};
  for (auto _ : state) {
    for (const auto &sample : data) {
      std::array<double, ActionCount> regret{};
      for (std::size_t action = 0U; action < ActionCount; ++action) {
        regret[action] = static_cast<double>(sample.loss[action]);
      }
      store_policy(regret, output);
      benchmark::DoNotOptimize(output);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sample_count));
}

template <std::size_t ActionCount, bool Omd, bool Sorted>
void bm_fd_update(benchmark::State &state) {
  const auto &data = samples<ActionCount>();
  std::array<float, ActionCount> output{};
  for (auto _ : state) {
    for (const auto &sample : data) {
      std::array<double, ActionCount> thresholds{};
      for (std::size_t action = 0U; action < ActionCount; ++action) {
        thresholds[action] = static_cast<double>(sample.loss[action]) -
                             (Omd ? static_cast<double>(sample.center[action]) : 0.0);
      }
      const double alpha = Sorted ? solve_sorted(thresholds, sample.lambda)
                                  : solve_direct(thresholds, sample.lambda);
      std::array<double, ActionCount> weights{};
      for (std::size_t action = 0U; action < ActionCount; ++action) {
        weights[action] = alpha - thresholds[action];
      }
      store_policy(weights, output);
      benchmark::DoNotOptimize(output);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sample_count));
}

template <std::size_t ActionCount> void bm_fd_ftrl_direct(benchmark::State &state) {
  bm_fd_update<ActionCount, false, false>(state);
}

template <std::size_t ActionCount> void bm_fd_ftrl_sorted(benchmark::State &state) {
  bm_fd_update<ActionCount, false, true>(state);
}

template <std::size_t ActionCount> void bm_fd_omd_direct(benchmark::State &state) {
  bm_fd_update<ActionCount, true, false>(state);
}

template <std::size_t ActionCount> void bm_fd_omd_sorted(benchmark::State &state) {
  bm_fd_update<ActionCount, true, true>(state);
}

template <std::size_t ActionCount> void bm_loads(benchmark::State &state) {
  const auto &data = samples<ActionCount>();
  for (auto _ : state) {
    for (const auto &sample : data) {
      std::array<double, ActionCount> loaded{};
      for (std::size_t action = 0U; action < ActionCount; ++action) {
        loaded[action] = static_cast<double>(sample.loss[action]) +
                         static_cast<double>(sample.center[action]);
      }
      benchmark::DoNotOptimize(loaded);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sample_count));
}

template <std::size_t ActionCount> void bm_regularizer(benchmark::State &state) {
  constexpr double eta = 0.01;
  constexpr double payoff_norm = 20.0;
  constexpr double horizon = 256.0;
  const auto &data = samples<ActionCount>();
  for (auto _ : state) {
    for (const auto &sample : data) {
      double lambda = eta * sample.reach * static_cast<double>(ActionCount) * payoff_norm *
                      payoff_norm * horizon;
      benchmark::DoNotOptimize(lambda);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sample_count));
}

template <std::size_t ActionCount, int Method> void bm_piecewise_solve(benchmark::State &state) {
  const auto &data = samples<ActionCount>();
  for (auto _ : state) {
    for (const auto &sample : data) {
      std::array<double, ActionCount> thresholds{};
      for (std::size_t action = 0U; action < ActionCount; ++action) {
        thresholds[action] = static_cast<double>(sample.loss[action]);
      }
      double alpha = Method == 0   ? solve_direct(thresholds, sample.lambda)
                     : Method == 1 ? solve_sorted(thresholds, sample.lambda)
                                   : solve_bisection(thresholds, sample.lambda);
      benchmark::DoNotOptimize(alpha);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sample_count));
}

template <std::size_t ActionCount> void bm_normalization(benchmark::State &state) {
  const auto &data = samples<ActionCount>();
  std::array<float, ActionCount> output{};
  for (auto _ : state) {
    for (const auto &sample : data) {
      std::array<double, ActionCount> weights{};
      for (std::size_t action = 0U; action < ActionCount; ++action) {
        weights[action] = static_cast<double>(sample.center[action]);
      }
      store_policy(weights, output);
      benchmark::DoNotOptimize(output);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sample_count));
}

#define GTOSD_REGISTER_ARITY(Label, Function)                                                       \
  BENCHMARK_TEMPLATE(Function, 2U)->Name(Label "/2")->Unit(benchmark::kNanosecond);                \
  BENCHMARK_TEMPLATE(Function, 3U)->Name(Label "/3")->Unit(benchmark::kNanosecond);                \
  BENCHMARK_TEMPLATE(Function, 4U)->Name(Label "/4")->Unit(benchmark::kNanosecond);                \
  BENCHMARK_TEMPLATE(Function, 5U)->Name(Label "/5")->Unit(benchmark::kNanosecond)

GTOSD_REGISTER_ARITY("local/regret_matching", bm_regret_matching);
GTOSD_REGISTER_ARITY("component/loads", bm_loads);
GTOSD_REGISTER_ARITY("component/regularizer", bm_regularizer);
GTOSD_REGISTER_ARITY("component/normalization_store", bm_normalization);

#define GTOSD_REGISTER_SOLVE(Label, Method)                                                          \
  BENCHMARK_TEMPLATE2(bm_piecewise_solve, 2U, Method)                                               \
      ->Name(Label "/2")                                                                           \
      ->Unit(benchmark::kNanosecond);                                                               \
  BENCHMARK_TEMPLATE2(bm_piecewise_solve, 3U, Method)                                               \
      ->Name(Label "/3")                                                                           \
      ->Unit(benchmark::kNanosecond);                                                               \
  BENCHMARK_TEMPLATE2(bm_piecewise_solve, 4U, Method)                                               \
      ->Name(Label "/4")                                                                           \
      ->Unit(benchmark::kNanosecond);                                                               \
  BENCHMARK_TEMPLATE2(bm_piecewise_solve, 5U, Method)                                               \
      ->Name(Label "/5")                                                                           \
      ->Unit(benchmark::kNanosecond)

GTOSD_REGISTER_SOLVE("component/direct_piecewise", 0);
GTOSD_REGISTER_SOLVE("component/sorted_piecewise", 1);
GTOSD_REGISTER_SOLVE("component/bisection", 2);

GTOSD_REGISTER_ARITY("local/fd_ftrl_direct", bm_fd_ftrl_direct);
GTOSD_REGISTER_ARITY("local/fd_ftrl_sorted", bm_fd_ftrl_sorted);
GTOSD_REGISTER_ARITY("local/fd_omd_direct", bm_fd_omd_direct);
GTOSD_REGISTER_ARITY("local/fd_omd_sorted", bm_fd_omd_sorted);

#undef GTOSD_REGISTER_SOLVE
#undef GTOSD_REGISTER_ARITY

} // namespace

BENCHMARK_MAIN();
