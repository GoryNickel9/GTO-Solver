#include "gtosd/abstraction/card_abstraction.hpp"
#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"
#include "gtosd/subgame/subgame_solver.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

gtosd::SolverConfig cfr_plus_config(const std::uint64_t iterations) {
  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::CfrPlus;
  config.iterations = iterations;
  config.averaging_delay = std::min<std::uint64_t>(100U, iterations / 10U);
  config.seed = 0x42454e4348414253ULL;
  config.thread_count = 8U;
  return config;
}

std::vector<gtosd::CardAbstractionObservation> kuhn_observations() {
  std::vector<gtosd::CardAbstractionObservation> observations;
  const std::vector<std::string> ranks{"J", "Q", "K"};
  const std::vector<double> strength{0.0, 0.5, 1.0};
  std::uint16_t combo = 200;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    const std::vector<std::string> histories =
        player == 0U ? std::vector<std::string>{"", "kb"} : std::vector<std::string>{"k", "b"};
    for (const auto &history : histories) {
      const std::string partition = "kuhn:p" + std::to_string(player) + ":h=" + history;
      for (std::size_t rank = 0; rank < ranks.size(); ++rank) {
        observations.push_back(
            {"kuhn:p" + std::to_string(player) + ":" + ranks[rank] + ":" + history,
             partition,
             player,
             combo++,
             0,
             1.0,
             {strength[rank]}});
      }
    }
  }
  return observations;
}

gtosd::FiniteGame make_abstract_kuhn() {
  gtosd::CardAbstractionConfig config;
  config.kind = gtosd::CardAbstractionKind::EquityFeatureKMeans;
  config.buckets_per_partition = 2;
  config.feature_schema_id = "benchmark-equity-strength-l2-v1";
  const auto abstraction = gtosd::build_card_abstraction(kuhn_observations(), config);
  if (!abstraction) {
    throw std::runtime_error("cannot build benchmark abstraction");
  }
  const auto game =
      gtosd::apply_card_abstraction(gtosd::make_kuhn_poker_game(), abstraction.value());
  if (!game) {
    throw std::runtime_error("cannot build benchmark abstract game");
  }
  return game.value();
}

gtosd::PostflopTreeConfig native_river_config() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  config.flop = {gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value(),
                 gtosd::parse_card("Qh").value()};
  config.turn = gtosd::parse_card("8s").value();
  config.river = gtosd::parse_card("9d").value();
  config.rake.enabled = false;
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.raise_depth = 0U;
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
      }
    }
  }
  return config;
}

gtosd::PostflopRanges native_river_ranges(const gtosd::PostflopTreeConfig &config) {
  gtosd::PostflopRanges ranges;
  const auto zero = gtosd::RangeWeight::from_basis_points(0U).value();
  const auto full = gtosd::RangeWeight::from_basis_points(10'000U).value();
  for (auto &range : ranges.players) {
    range.fill(zero);
  }
  const auto board_mask = config.flop[0].mask() | config.flop[1].mask() | config.flop[2].mask() |
                          (config.turn ? config.turn->mask() : 0U) |
                          (config.river ? config.river->mask() : 0U);
  std::vector<gtosd::CardId> available;
  for (const auto card : gtosd::short_deck()) {
    if ((card.mask() & board_mask) == 0U) {
      available.push_back(card);
    }
  }
  const auto combos = gtosd::all_combos();
  for (std::size_t card = 0U; card + 1U < available.size() && card < 24U; card += 2U) {
    const auto found = std::ranges::find_if(combos, [&](const gtosd::Combo &combo) {
      return (combo.first == available[card] && combo.second == available[card + 1U]) ||
             (combo.first == available[card + 1U] && combo.second == available[card]);
    });
    if (found == combos.end()) {
      throw std::runtime_error("cannot build native benchmark ranges");
    }
    const auto combo = static_cast<std::size_t>(std::distance(combos.begin(), found));
    ranges.players[0][combo] = full;
    ranges.players[1][combo] = full;
  }
  return ranges;
}

gtosd::PostflopSolveOptions native_cfr_plus_options(const std::uint64_t iterations) {
  gtosd::PostflopSolveOptions options;
  options.iterations = iterations;
  options.certification_interval = iterations;
  options.algorithm = gtosd::PostflopAlgorithm::CfrPlus;
  options.state_precision = gtosd::PostflopStatePrecision::Float64;
  options.parallel_action_depth = gtosd::production_postflop_parallel_workers;
  return options;
}

void BM_CfrPlusKuhnExact(benchmark::State &state) {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto config = cfr_plus_config(static_cast<std::uint64_t>(state.range(0)));
  for (auto _ : state) {
    static_cast<void>(_);
    const auto solved = gtosd::solve_finite_game(game, config);
    if (!solved) {
      state.SkipWithError("exact CFR+ solve failed");
      break;
    }
    auto completed = solved.value().checkpoint.completed_iterations;
    benchmark::DoNotOptimize(completed);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_CfrPlusKuhnBucketed(benchmark::State &state) {
  const auto game = make_abstract_kuhn();
  const auto config = cfr_plus_config(static_cast<std::uint64_t>(state.range(0)));
  for (auto _ : state) {
    static_cast<void>(_);
    const auto solved = gtosd::solve_finite_game(game, config);
    if (!solved) {
      state.SkipWithError("bucketed CFR+ solve failed");
      break;
    }
    auto completed = solved.value().checkpoint.completed_iterations;
    benchmark::DoNotOptimize(completed);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_ExactGuardedSubgame(benchmark::State &state) {
  // Leduc's root chance fanout is wider than the production pool, so this
  // benchmark exercises all eight configured CFR+ threads rather than merely
  // recording an eight-thread config on a single decision-root toy game.
  const auto game = gtosd::make_leduc_poker_game();
  const auto blueprint = gtosd::uniform_strategy_profile(game);
  if (!blueprint) {
    state.SkipWithError("blueprint build failed");
    return;
  }
  gtosd::SubgameSolveConfig config;
  config.solver = cfr_plus_config(static_cast<std::uint64_t>(state.range(0)));
  config.safety = gtosd::SubgameSafetyMode::ExactNashConvGuard;
  for (auto _ : state) {
    static_cast<void>(_);
    const auto solved = gtosd::solve_subgame(game, blueprint.value(), {{game.root, 1.0}}, config);
    if (!solved || !solved.value().deployed_metrics.has_value()) {
      state.SkipWithError("safe subgame solve failed");
      break;
    }
    auto nash_conv = solved.value().deployed_metrics->nash_conv;
    benchmark::DoNotOptimize(nash_conv);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_NativePostflopCfrPlusExact(benchmark::State &state) {
  const auto config = native_river_config();
  const auto ranges = native_river_ranges(config);
  const auto options = native_cfr_plus_options(static_cast<std::uint64_t>(state.range(0)));
  for (auto _ : state) {
    static_cast<void>(_);
    const auto solved = gtosd::solve_postflop_exact(config, ranges, options);
    if (!solved) {
      state.SkipWithError("native exact postflop solve failed");
      break;
    }
    state.counters["state_bytes"] =
        benchmark::Counter(static_cast<double>(solved.value().actions * 2U * sizeof(double)));
    auto normalized_nash_conv = solved.value().convergence.back().normalized_nash_conv;
    benchmark::DoNotOptimize(normalized_nash_conv);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_NativePostflopTurnFeatureCacheBuild(benchmark::State &state) {
  auto config = native_river_config();
  config.river.reset();
  const auto ranges = native_river_ranges(config);
  for (auto _ : state) {
    static_cast<void>(_);
    const auto cache = gtosd::build_postflop_card_abstraction_feature_cache(config, ranges);
    if (!cache) {
      state.SkipWithError("native turn feature cache build failed");
      break;
    }
    state.counters["partitions"] =
        benchmark::Counter(static_cast<double>(cache.value().partition_count));
    state.counters["observations"] =
        benchmark::Counter(static_cast<double>(cache.value().observations.size()));
    auto fingerprint = cache.value().fingerprint;
    benchmark::DoNotOptimize(fingerprint);
  }
}

void BM_NativePostflopCfrPlusBucketed(benchmark::State &state) {
  const auto config = native_river_config();
  const auto ranges = native_river_ranges(config);
  const auto options = native_cfr_plus_options(static_cast<std::uint64_t>(state.range(0)));
  const auto feature_cache = gtosd::build_postflop_card_abstraction_feature_cache(config, ranges);
  if (!feature_cache) {
    state.SkipWithError("native feature cache setup failed");
    return;
  }
  gtosd::CardAbstractionConfig abstraction;
  abstraction.kind = gtosd::CardAbstractionKind::EquityFeatureKMeans;
  abstraction.buckets_per_partition = static_cast<std::uint32_t>(state.range(1));
  abstraction.maximum_iterations = 50U;
  for (auto _ : state) {
    static_cast<void>(_);
    const auto solved = gtosd::solve_postflop_abstracted(config, ranges, abstraction,
                                                         feature_cache.value(), options);
    if (!solved || !solved.value().card_abstraction.has_value()) {
      state.SkipWithError("native bucketed postflop solve failed");
      break;
    }
    state.counters["state_bytes"] =
        benchmark::Counter(static_cast<double>(solved.value().actions * 2U * sizeof(double)));
    state.counters["compression"] =
        benchmark::Counter(solved.value().card_abstraction->metrics.compression_ratio);
    state.counters["exact_infosets"] = benchmark::Counter(
        static_cast<double>(solved.value().card_abstraction->metrics.exact_information_sets));
    state.counters["abstract_infosets"] = benchmark::Counter(
        static_cast<double>(solved.value().card_abstraction->metrics.abstract_information_sets));
    state.counters["weighted_mse"] =
        benchmark::Counter(solved.value().card_abstraction->metrics.weighted_mean_squared_error);
    state.counters["maximum_l2"] =
        benchmark::Counter(solved.value().card_abstraction->metrics.maximum_l2_error);
    state.counters["feature_ms"] =
        benchmark::Counter(solved.value().card_abstraction->feature_preparation_seconds * 1'000.0);
    state.counters["cluster_ms"] =
        benchmark::Counter(solved.value().card_abstraction->clustering_seconds * 1'000.0);
    state.counters["nash_conv_pct"] =
        benchmark::Counter(solved.value().convergence.back().normalized_nash_conv * 100.0);
    auto normalized_nash_conv = solved.value().convergence.back().normalized_nash_conv;
    benchmark::DoNotOptimize(normalized_nash_conv);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_CfrPlusKuhnExact)->Arg(1'000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_CfrPlusKuhnBucketed)->Arg(1'000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ExactGuardedSubgame)->Arg(1'000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_NativePostflopCfrPlusExact)->Arg(1'000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_NativePostflopTurnFeatureCacheBuild)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_NativePostflopCfrPlusBucketed)
    ->Args({1'000, 1})
    ->Args({1'000, 2})
    ->Args({1'000, 3})
    ->Args({1'000, 6})
    ->Args({1'000, 12})
    ->Unit(benchmark::kMillisecond);

} // namespace

BENCHMARK_MAIN();
