#include "gtosd/equity/evaluator.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <vector>

namespace {

void BM_EvaluateSeven(benchmark::State &state) {
  const auto deck = gtosd::short_deck();
  const std::array<gtosd::CardId, 7> cards{deck[0],  deck[1],  deck[6], deck[11],
                                           deck[16], deck[25], deck[34]};
  for (auto _ : state) {
    static_cast<void>(_);
    auto value = gtosd::evaluate_seven(cards);
    benchmark::DoNotOptimize(value);
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_EvaluateSeven);

std::vector<std::array<gtosd::CardId, 7>> make_batch_fixture() {
  constexpr std::size_t batch_size = 4'096U;
  const auto deck = gtosd::short_deck();
  std::vector<std::array<gtosd::CardId, 7>> hands;
  hands.reserve(batch_size);
  for (std::size_t hand_index = 0; hand_index < batch_size; ++hand_index) {
    std::array<gtosd::CardId, 7> cards{};
    const auto offset = (hand_index * 7U) % deck.size();
    for (std::size_t card_index = 0; card_index < cards.size(); ++card_index) {
      cards[card_index] = deck[(offset + card_index) % deck.size()];
    }
    hands.push_back(cards);
  }
  return hands;
}

void BM_EvaluateSevenBatch(benchmark::State &state) {
  const auto hands = make_batch_fixture();
  for (auto _ : state) {
    static_cast<void>(_);
    auto values = gtosd::evaluate_seven_batch(hands);
    benchmark::DoNotOptimize(values);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(hands.size()));
}

BENCHMARK(BM_EvaluateSevenBatch);

gtosd::CanonicalStateInput make_isomorphism_fixture() {
  const auto parse = [](const std::string_view text) { return gtosd::parse_card(text).value(); };
  gtosd::CanonicalStateInput input;
  input.public_state.board_mask = parse("As").mask() | parse("Qd").mask() | parse("7c").mask();
  input.betting_history = "CO:X|BTN:B5000|CO:C";
  input.ranges[0] = {
      {{parse("Ks"), parse("Js")}, gtosd::RangeWeight::from_basis_points(10'000).value()},
      {{parse("Ac"), parse("Kc")}, gtosd::RangeWeight::from_basis_points(4'000).value()}};
  input.ranges[1] = {
      {{parse("9h"), parse("8h")}, gtosd::RangeWeight::from_basis_points(7'500).value()}};
  return input;
}

void BM_CanonicalizeGlobalState(benchmark::State &state) {
  const auto input = make_isomorphism_fixture();
  for (auto _ : state) {
    static_cast<void>(_);
    auto canonical = gtosd::canonicalize_state(input);
    benchmark::DoNotOptimize(canonical);
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_CanonicalizeGlobalState);

void BM_CanonicalKeyCacheHit(benchmark::State &state) {
  const auto input = make_isomorphism_fixture();
  gtosd::CanonicalKeyCache cache;
  benchmark::DoNotOptimize(cache.canonicalize(input));
  for (auto _ : state) {
    static_cast<void>(_);
    auto canonical = cache.canonicalize(input);
    benchmark::DoNotOptimize(canonical);
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_CanonicalKeyCacheHit);

void BM_KuhnDcfrIterations(benchmark::State &state) {
  const auto game = gtosd::make_kuhn_poker_game();
  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::Dcfr;
  config.iterations = 500;
  for (auto _ : state) {
    static_cast<void>(_);
    auto solved = gtosd::solve_finite_game(game, config);
    benchmark::DoNotOptimize(solved);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(config.iterations));
}

BENCHMARK(BM_KuhnDcfrIterations);

} // namespace
