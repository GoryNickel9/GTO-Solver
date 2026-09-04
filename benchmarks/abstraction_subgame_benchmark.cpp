#include "gtosd/abstraction/card_abstraction.hpp"
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
  const auto game = gtosd::make_matching_pennies_game();
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

BENCHMARK(BM_CfrPlusKuhnExact)->Arg(1'000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_CfrPlusKuhnBucketed)->Arg(1'000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ExactGuardedSubgame)->Arg(1'000)->Unit(benchmark::kMillisecond);

} // namespace

BENCHMARK_MAIN();
