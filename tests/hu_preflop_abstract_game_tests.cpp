#include "gtosd/preflop/hu_preflop_abstract_game.hpp"
#include "gtosd/solver/best_response.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::HuPreflopTree hu10_tree() {
  auto config = gtosd::make_hu_co40_benchmark_config();
  config.effective_stack = gtosd::Money::from_units(100'000).value();
  config.open_targets = {gtosd::Money::from_units(30'000).value(),
                         gtosd::Money::from_units(50'000).value()};
  config.response_targets = {gtosd::Money::from_units(60'000).value(),
                             gtosd::Money::from_units(80'000).value()};
  const auto tree = gtosd::build_hu_preflop_tree(config);
  if (!tree) {
    throw std::runtime_error("HU10 tree build failed");
  }
  return tree.value();
}

gtosd::HuPreflopSolveOptions v23_options() {
  gtosd::HuPreflopSolveOptions options;
  options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23;
  options.equity_samples_per_bucket = 4U;
  options.partition_seed = 0x5632'3350'4152'5401ULL;
  options.distributional_bucket_capacities = {32U, 128U, 512U};
  return options;
}

void test_hu10_empirical_chance_compiler() {
  const auto tree = hu10_tree();
  const auto options = v23_options();
  constexpr auto corpus_seed = 0x5632'3343'4F52'5001ULL;
  constexpr auto maximum_nodes = 250'000U;
  const auto first =
      gtosd::compile_hu_preflop_abstract_game(tree, options, 1U, corpus_seed, maximum_nodes);
  const auto repeated =
      gtosd::compile_hu_preflop_abstract_game(tree, options, 1U, corpus_seed, maximum_nodes);
  require(first.has_value() && repeated.has_value(),
          "HU10 finite empirical abstract games compile");
  require(first.value().chance_corpus.outcomes.size() == gtosd::hu_preflop_hand_class_count &&
              first.value().game.nodes[first.value().game.root].edges.size() ==
                  gtosd::hu_preflop_hand_class_count,
          "one stratified chance outcome is compiled for every CO class");
  const auto chance_probability = std::accumulate(
      first.value().chance_corpus.outcomes.begin(), first.value().chance_corpus.outcomes.end(), 0.0,
      [](const double total, const auto &outcome) { return total + outcome.probability; });
  require(std::abs(chance_probability - 1.0) <= 1.0e-12,
          "stratified chance probabilities sum to one");
  require(first.value().chance_corpus.fingerprint == repeated.value().chance_corpus.fingerprint &&
              first.value().summary.fingerprint == repeated.value().summary.fingerprint &&
              first.value().summary.nodes == repeated.value().summary.nodes &&
              first.value().summary.information_sets == repeated.value().summary.information_sets,
          "fixed V23 seeds reproduce corpus, game and census fingerprints");
  require(first.value().summary.nodes > 100'000U && first.value().summary.nodes < maximum_nodes &&
              first.value().summary.information_sets > 1'000U,
          "HU10 compiler materializes a bounded nontrivial whole game");

  const auto uniform = gtosd::uniform_strategy_profile(first.value().game);
  const auto uniform_nashconv =
      uniform ? gtosd::calculate_nash_conv(first.value().game, uniform.value())
              : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                    gtosd::SolverError::InvalidStrategy);
  require(uniform_nashconv.has_value() && std::isfinite(uniform_nashconv.value().nash_conv) &&
              uniform_nashconv.value().nash_conv >= 0.0,
          "exact NashConv evaluates the complete HU10 empirical game");
  for (std::size_t player = 0U; player < 2U; ++player) {
    require(uniform_nashconv.value().best_response_value[player] + 1.0e-12 >=
                uniform_nashconv.value().profile_value[player],
            "HU10 exact best response never underperforms the frozen profile");
  }

  gtosd::SolverConfig solver;
  solver.algorithm = gtosd::SolverAlgorithm::LinearMccfr;
  solver.iterations = 16U;
  solver.seed = 0x5632'3353'4F4C'5601ULL;
  const auto continuous = gtosd::solve_finite_game(first.value().game, solver);
  solver.iterations = 8U;
  const auto partial = gtosd::solve_finite_game(first.value().game, solver);
  solver.iterations = 16U;
  const auto resumed =
      partial ? gtosd::solve_finite_game(first.value().game, solver, &partial.value().checkpoint)
              : gtosd::Result<gtosd::SolveResult, gtosd::SolverError>::failure(
                    gtosd::SolverError::InvalidCheckpoint);
  const auto continuous_bytes =
      continuous ? gtosd::serialize_solver_checkpoint(continuous.value().checkpoint)
                 : gtosd::Result<std::string, gtosd::SolverError>::failure(
                       gtosd::SolverError::InvalidCheckpoint);
  const auto resumed_bytes = resumed
                                 ? gtosd::serialize_solver_checkpoint(resumed.value().checkpoint)
                                 : gtosd::Result<std::string, gtosd::SolverError>::failure(
                                       gtosd::SolverError::InvalidCheckpoint);
  require(continuous_bytes.has_value() && resumed_bytes.has_value() &&
              continuous_bytes.value() == resumed_bytes.value(),
          "HU10 Linear MCCFR resume is byte-identical to the continuous run");

  const auto different_seed =
      gtosd::compile_hu_preflop_abstract_game(tree, options, 1U, corpus_seed + 1U, maximum_nodes);
  require(different_seed.has_value() &&
              different_seed.value().chance_corpus.fingerprint !=
                  first.value().chance_corpus.fingerprint &&
              different_seed.value().summary.fingerprint != first.value().summary.fingerprint,
          "changing the chance corpus seed changes the certified game identity");
  const auto prefix_corpus =
      gtosd::compile_hu_preflop_abstract_game(tree, options, 2U, corpus_seed, 400'000U);
  bool prefix_stable = prefix_corpus.has_value();
  if (prefix_stable) {
    for (std::size_t hand_class_id = 0U; hand_class_id < gtosd::hu_preflop_hand_class_count;
         ++hand_class_id) {
      const auto &one = first.value().chance_corpus.outcomes[hand_class_id];
      const auto &two = prefix_corpus.value().chance_corpus.outcomes[hand_class_id * 2U];
      prefix_stable = prefix_stable && one.deal == two.deal;
    }
  }
  require(prefix_stable,
          "increasing K with the same seed preserves the first deal of every CO stratum");
  const auto cross_corpus_completion =
      continuous && different_seed
          ? gtosd::complete_strategy_profile(different_seed.value().game,
                                             continuous.value().average_strategy,
                                             gtosd::PolicyCompletionRule::UniformUnseenV1)
          : gtosd::Result<gtosd::CompletedStrategyProfile, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidStrategy);
  require(cross_corpus_completion.has_value(),
          "a frozen V23 policy completes explicitly on an independent corpus");
  require(cross_corpus_completion.value().coverage.target_game_fingerprint ==
                  different_seed.value().summary.fingerprint &&
              cross_corpus_completion.value().coverage.unseen_information_sets > 0U &&
              cross_corpus_completion.value().coverage.unused_supplied_information_sets > 0U &&
              cross_corpus_completion.value().coverage.exact_key_coverage < 1.0 &&
              cross_corpus_completion.value().coverage.reach_weighted_coverage < 1.0,
          "cross-corpus audit exposes unseen, unused and reach-weighted policy support");
  std::cout << "cross_corpus_exact_key_coverage="
            << cross_corpus_completion.value().coverage.exact_key_coverage << '\n'
            << "cross_corpus_reach_weighted_coverage="
            << cross_corpus_completion.value().coverage.reach_weighted_coverage << '\n'
            << "cross_corpus_unseen_information_sets="
            << cross_corpus_completion.value().coverage.unseen_information_sets << '\n';
  const auto cross_corpus_rejected =
      continuous && different_seed
          ? gtosd::complete_strategy_profile(different_seed.value().game,
                                             continuous.value().average_strategy,
                                             gtosd::PolicyCompletionRule::RejectMissing)
          : gtosd::Result<gtosd::CompletedStrategyProfile, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidStrategy);
  require(!cross_corpus_rejected &&
              cross_corpus_rejected.error() == gtosd::SolverError::InvalidStrategy,
          "strict completion rejects a V23 policy with cross-corpus support gaps");
  const auto bounded =
      gtosd::compile_hu_preflop_abstract_game(tree, options, 1U, corpus_seed, 100'000U);
  require(!bounded && bounded.error() == gtosd::HuPreflopError::NodeOverflow,
          "finite compiler rejects a game beyond the explicit node budget");
}

} // namespace

int main() {
  try {
    test_hu10_empirical_chance_compiler();
    std::cout << "V23_HU_PREFLOP_ABSTRACT_GAME_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "V23_HU_PREFLOP_ABSTRACT_GAME_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "V23_HU_PREFLOP_ABSTRACT_GAME_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
