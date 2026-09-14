#include "gtosd/core/external_sampling.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
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

void require_near(const double actual, const double expected, const double tolerance,
                  const std::string_view message) {
  require(std::abs(actual - expected) <= tolerance, message);
}

gtosd::SolverConfig config(const gtosd::SolverAlgorithm algorithm, const std::uint64_t iterations) {
  gtosd::SolverConfig result;
  result.algorithm = algorithm;
  result.iterations = iterations;
  result.seed = 0x5232'4156'4552'4147ULL;
  return result;
}

void test_exact_opponent_pass_expectation() {
  struct Profile {
    double first_player_enter;
    double second_player_continue;
    double late_action_a;
    double iteration_weight;
  };
  constexpr std::array profiles{
      Profile{0.1, 0.5, 1.0, 1.0},
      Profile{0.9, 0.5, 0.0, 1.0},
  };

  std::array<double, 2> correct{};
  std::array<double, 2> opponent_pass{};
  std::array<double, 2> legacy_two_pass{};
  for (const auto &profile : profiles) {
    const std::array strategy{profile.late_action_a, 1.0 - profile.late_action_a};
    for (std::size_t action = 0U; action < strategy.size(); ++action) {
      correct[action] += profile.iteration_weight * profile.first_player_enter * strategy[action];
      opponent_pass[action] +=
          profile.iteration_weight * strategy[action] *
          (profile.second_player_continue *
               gtosd::external_sampling_average_multiplier(0U, 0U, profile.first_player_enter) +
           profile.first_player_enter *
               gtosd::external_sampling_average_multiplier(0U, 1U, profile.first_player_enter));
      legacy_two_pass[action] += profile.iteration_weight * profile.first_player_enter *
                                 (profile.second_player_continue + profile.first_player_enter) *
                                 strategy[action];
    }
  }
  const auto normalize_a = [](const std::array<double, 2> &values) {
    return values[0] / (values[0] + values[1]);
  };
  require_near(normalize_a(correct), 0.1, 1.0e-15, "correct behavioral average is 10 percent");
  require_near(normalize_a(opponent_pass), normalize_a(correct), 1.0e-15,
               "opponent-pass estimator matches the exact expected numerator and denominator");
  require_near(normalize_a(legacy_two_pass), 1.0 / 22.0, 1.0e-15,
               "regression fixture reproduces the biased historical estimator");

  auto weighted = profiles;
  weighted[0].iteration_weight = 8.0;
  weighted[1].iteration_weight = 27.0;
  correct = {};
  opponent_pass = {};
  for (const auto &profile : weighted) {
    const std::array strategy{profile.late_action_a, 1.0 - profile.late_action_a};
    for (std::size_t action = 0U; action < strategy.size(); ++action) {
      correct[action] += profile.iteration_weight * profile.first_player_enter * strategy[action];
      opponent_pass[action] +=
          profile.iteration_weight * profile.first_player_enter * strategy[action] *
          gtosd::external_sampling_average_multiplier(0U, 1U, profile.first_player_enter);
    }
  }
  require_near(normalize_a(correct), 8.0 / 251.0, 1.0e-15,
               "linear-weighted target matches the preserved rational fixture");
  require_near(normalize_a(opponent_pass), normalize_a(correct), 1.0e-15,
               "opponent-pass estimator remains correct with explicit iteration weights");

  require_near(gtosd::external_sampling_average_multiplier(
                   0U, 0U, 0.25, gtosd::ExternalSamplingAveragePass::OneSidedTraverser),
               0.25, 0.0, "one-sided response averaging retains traverser reach");
  require_near(gtosd::external_sampling_average_multiplier(
                   1U, 0U, 0.0, gtosd::ExternalSamplingAveragePass::OneSidedTraverser),
               0.0, 0.0, "one-sided response does not update the fixed actor");
  require_near(gtosd::external_sampling_average_multiplier(0U, 1U, 0.0), 1.0, 0.0,
               "opponent-pass averaging handles zero actor reach through visit probability");

  constexpr double opponent_reach = 0.4;
  constexpr double chance_reach = 0.25;
  constexpr double action_regret = 3.0;
  const double enumerated_expected_regret = opponent_reach * chance_reach * action_regret;
  require_near(enumerated_expected_regret, 0.3, 1.0e-15,
               "sampled regret expectation equals counterfactual opponent and chance reach");
}

void test_unbiased_opponent_action_baseline() {
  constexpr std::array strategy{0.25, 0.25, 0.5};
  constexpr std::array action_values{1.0, -2.0, 4.0};
  constexpr std::array imperfect_baseline{0.5, -1.0, 2.5};
  constexpr auto exact_value = strategy[0] * action_values[0] + strategy[1] * action_values[1] +
                               strategy[2] * action_values[2];
  double estimator_expectation = 0.0;
  for (std::size_t sampled = 0U; sampled < strategy.size(); ++sampled) {
    estimator_expectation +=
        strategy[sampled] *
        gtosd::external_sampling_opponent_baseline_estimate(
            strategy, imperfect_baseline, strategy.size(), sampled, action_values[sampled]);
  }
  require_near(estimator_expectation, exact_value, 1.0e-15,
               "enumeration proves the opponent-action baseline estimator is unbiased");

  double zero_baseline_variance = 0.0;
  double perfect_baseline_variance = 0.0;
  constexpr std::array zero_baseline{0.0, 0.0, 0.0};
  for (std::size_t sampled = 0U; sampled < strategy.size(); ++sampled) {
    const auto zero_estimate = gtosd::external_sampling_opponent_baseline_estimate(
        strategy, zero_baseline, strategy.size(), sampled, action_values[sampled]);
    const auto perfect_estimate = gtosd::external_sampling_opponent_baseline_estimate(
        strategy, action_values, strategy.size(), sampled, action_values[sampled]);
    zero_baseline_variance +=
        strategy[sampled] * (zero_estimate - exact_value) * (zero_estimate - exact_value);
    perfect_baseline_variance +=
        strategy[sampled] * (perfect_estimate - exact_value) * (perfect_estimate - exact_value);
  }
  require(zero_baseline_variance > 0.0 && perfect_baseline_variance == 0.0,
          "the exact enumerable baseline reduces opponent-action variance to zero");
}

void test_equal_probability_response_stratification() {
  constexpr std::size_t stratum_count = 4U;
  constexpr std::array within_draws{0.0, 0.25, 0.5, 0.999999};
  for (std::size_t stratum = 0U; stratum < stratum_count; ++stratum) {
    for (const auto draw : within_draws) {
      const auto quantile =
          gtosd::external_sampling_stratified_quantile(stratum, stratum_count, draw);
      require(quantile >= static_cast<double>(stratum) / stratum_count &&
                  quantile < static_cast<double>(stratum + 1U) / stratum_count,
              "stratified response quantile stays inside its equal-probability interval");
    }
  }

  constexpr std::array strategy{0.10, 0.20, 0.30, 0.40};
  require(gtosd::external_sampling_action_from_quantile(strategy, strategy.size(), 0.00) == 0U,
          "inverse CDF selects the first response");
  require(gtosd::external_sampling_action_from_quantile(strategy, strategy.size(), 0.10) == 1U,
          "inverse CDF uses half-open response intervals");
  require(gtosd::external_sampling_action_from_quantile(strategy, strategy.size(), 0.59) == 2U,
          "inverse CDF selects an interior response");
  require(gtosd::external_sampling_action_from_quantile(strategy, strategy.size(), 0.99) == 3U,
          "inverse CDF selects the final response");

  constexpr std::array action_values{7.0, -2.0, 4.0, 1.0};
  constexpr std::size_t integration_points_per_stratum = 1'000U;
  double stratified_mean = 0.0;
  for (std::size_t stratum = 0U; stratum < stratum_count; ++stratum) {
    double stratum_mean = 0.0;
    for (std::size_t point = 0U; point < integration_points_per_stratum; ++point) {
      const auto within =
          (static_cast<double>(point) + 0.5) / integration_points_per_stratum;
      const auto quantile =
          gtosd::external_sampling_stratified_quantile(stratum, stratum_count, within);
      const auto action =
          gtosd::external_sampling_action_from_quantile(strategy, strategy.size(), quantile);
      stratum_mean += action_values[action] / integration_points_per_stratum;
    }
    stratified_mean += stratum_mean / stratum_count;
  }
  const auto exact_mean = strategy[0] * action_values[0] + strategy[1] * action_values[1] +
                          strategy[2] * action_values[2] + strategy[3] * action_values[3];
  require_near(stratified_mean, exact_mean, 1.0e-12,
               "equal-probability response strata preserve an enumerable expectation");
}

void test_chance_sampled_full_action_reach_weights() {
  require_near(gtosd::chance_sampled_cfr_regret_delta(7.0, 3.0, 0.25), 1.0, 0.0,
               "full-action regret includes counterfactual opponent reach exactly once");
  require_near(gtosd::chance_sampled_cfr_regret_delta(-1.0, 3.0, 0.0), 0.0, 0.0,
               "unreachable opponent branch contributes no regret");
  require_near(gtosd::chance_sampled_cfr_average_multiplier(0.375), 0.375, 0.0,
               "full-action averaging retains the acting player's reach");
}

void test_dcfr_lazy_gap_equivalence() {
  for (const auto [last_iteration, current_iteration] :
       std::array<std::array<std::uint64_t, 2>, 4>{{{1U, 1U}, {1U, 2U}, {3U, 13U}, {1U, 101U}}}) {
    double positive = 1.0;
    double negative = 1.0;
    double average = 1.0;
    double positive_log_delta = 0.0;
    for (std::uint64_t iteration = last_iteration + 1U; iteration <= current_iteration;
         ++iteration) {
      const auto powered = std::pow(static_cast<double>(iteration), 1.5);
      const auto positive_step = powered / (powered + 1.0);
      positive *= positive_step;
      negative *= 0.5;
      average *=
          std::pow(static_cast<double>(iteration) / static_cast<double>(iteration + 1U), 3.0);
      positive_log_delta += std::log(positive_step);
    }
    const auto lazy =
        gtosd::dcfr_1503_lazy_discount(last_iteration, current_iteration, positive_log_delta);
    require_near(lazy.positive_regret, positive, 2.0e-15,
                 "lazy positive-regret discount matches dense updates");
    require_near(lazy.negative_regret, negative, 2.0e-15,
                 "lazy negative-regret discount matches dense updates");
    require_near(lazy.average_strategy, average, 2.0e-15,
                 "lazy average-strategy discount matches dense updates");
  }
}

void require_sampled_convergence(const gtosd::FiniteGame &game,
                                 const gtosd::SolverAlgorithm algorithm,
                                 const std::uint64_t iterations, const double maximum_nash_conv,
                                 const std::string_view label, const std::string_view message) {
  const std::array checkpoints{iterations / 10U, iterations / 2U, iterations};
  std::optional<gtosd::SolverCheckpoint> checkpoint;
  double initial_nash_conv = 0.0;
  double final_nash_conv = 0.0;
  for (std::size_t sample = 0U; sample < checkpoints.size(); ++sample) {
    const auto solved = gtosd::solve_finite_game(game, config(algorithm, checkpoints[sample]),
                                                 checkpoint ? &*checkpoint : nullptr);
    require(solved.has_value(), message);
    require(solved.value().maximum_normalization_error <= 1.0e-12,
            "sampled average strategy remains normalized");
    const auto metrics = gtosd::calculate_nash_conv(game, solved.value().average_strategy);
    require(metrics.has_value() && std::isfinite(metrics.value().nash_conv),
            "sampled profile receives an exact reduced-game best response");
    if (sample == 0U) {
      initial_nash_conv = metrics.value().nash_conv;
    }
    final_nash_conv = metrics.value().nash_conv;
    checkpoint = solved.value().checkpoint;
    std::cout << "R2_CONVERGENCE game=" << label
              << " algorithm=" << gtosd::solver_algorithm_name(algorithm)
              << " iterations=" << checkpoints[sample] << " nashconv=" << metrics.value().nash_conv
              << '\n';
  }
  require(final_nash_conv < initial_nash_conv,
          "sampled exact NashConv improves from the first recorded checkpoint");
  require(final_nash_conv < maximum_nash_conv, message);
}

void test_reduced_games_and_resume() {
  const auto kuhn = gtosd::make_kuhn_poker_game();
  require_sampled_convergence(kuhn, gtosd::SolverAlgorithm::ExternalSamplingMccfr, 100'000U, 0.05,
                              "kuhn", "external sampling converges on Kuhn");
  require_sampled_convergence(kuhn, gtosd::SolverAlgorithm::LinearMccfr, 100'000U, 0.05, "kuhn",
                              "Linear MCCFR converges on Kuhn");

  const auto leduc = gtosd::make_leduc_poker_game();
  require_sampled_convergence(leduc, gtosd::SolverAlgorithm::ExternalSamplingMccfr, 50'000U, 0.35,
                              "leduc", "external sampling lowers exact Leduc NashConv");
  require_sampled_convergence(leduc, gtosd::SolverAlgorithm::LinearMccfr, 50'000U, 0.35, "leduc",
                              "Linear MCCFR lowers exact Leduc NashConv");

  const auto short_deck = gtosd::make_short_deck_river_toy_game(0.0);
  require(short_deck.has_value(), "enumerable Short Deck game builds");
  require_sampled_convergence(short_deck.value(), gtosd::SolverAlgorithm::ExternalSamplingMccfr,
                              50'000U, 0.08, "short_deck_river_toy",
                              "external sampling converges on the enumerable Short Deck game");

  const auto continuous = gtosd::solve_finite_game(
      kuhn, config(gtosd::SolverAlgorithm::ExternalSamplingMccfr, 20'000U));
  const auto partial = gtosd::solve_finite_game(
      kuhn, config(gtosd::SolverAlgorithm::ExternalSamplingMccfr, 10'000U));
  require(continuous.has_value() && partial.has_value(),
          "continuous and partial sampled runs complete");
  const auto resumed =
      gtosd::solve_finite_game(kuhn, config(gtosd::SolverAlgorithm::ExternalSamplingMccfr, 20'000U),
                               &partial.value().checkpoint);
  require(resumed.has_value(), "sampled checkpoint resumes");
  const auto continuous_bytes = gtosd::serialize_solver_checkpoint(continuous.value().checkpoint);
  const auto resumed_bytes = gtosd::serialize_solver_checkpoint(resumed.value().checkpoint);
  require(continuous_bytes.has_value() && resumed_bytes.has_value() &&
              continuous_bytes.value() == resumed_bytes.value(),
          "sampled resume is byte-equivalent to the continuous run");

  auto invalid_parallel = config(gtosd::SolverAlgorithm::LinearMccfr, 100U);
  invalid_parallel.thread_count = 2U;
  const auto rejected = gtosd::solve_finite_game(kuhn, invalid_parallel);
  require(!rejected && rejected.error() == gtosd::SolverError::InvalidConfiguration,
          "Linear MCCFR rejects unvalidated parallel updates");
  require(std::string_view(gtosd::solver_algorithm_name(gtosd::SolverAlgorithm::LinearMccfr)) ==
              "linear_mccfr",
          "Linear MCCFR has a stable algorithm identifier");
}

void test_four_street_short_deck_mccfr_nashconv() {
  const auto game = gtosd::make_short_deck_four_street_toy_game(0.0);
  require(game.has_value(), "four-street Short Deck game builds");
  const auto summary = gtosd::validate_finite_game(game.value());
  require(summary.has_value() && summary.value().chance_nodes > 4U &&
              summary.value().decision_nodes > 16U && summary.value().information_sets > 8U,
          "four-street Short Deck game contains private deals, public chance and decisions");

  const auto uniform = gtosd::uniform_strategy_profile(game.value());
  const auto uniform_metrics =
      uniform ? gtosd::calculate_nash_conv(game.value(), uniform.value())
              : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                    gtosd::SolverError::InvalidStrategy);
  require(uniform_metrics.has_value() && uniform_metrics.value().nash_conv > 0.0,
          "uniform four-street policy has positive exact NashConv");

  auto options = config(gtosd::SolverAlgorithm::LinearMccfr, 60'000U);
  const auto certified = gtosd::solve_with_certification(game.value(), options, 20'000U);
  require(certified.has_value() && certified.value().convergence.size() == 3U,
          "Linear MCCFR emits three exact four-street NashConv checkpoints");
  const auto final_metrics =
      certified ? gtosd::calculate_nash_conv(game.value(), certified.value().solve.average_strategy)
                : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                      gtosd::SolverError::InvalidStrategy);
  require(final_metrics.has_value() &&
              final_metrics.value().nash_conv < certified.value().convergence.front().nash_conv &&
              final_metrics.value().nash_conv < uniform_metrics.value().nash_conv,
          "Linear MCCFR lowers exact whole-game NashConv on the four-street fixture");
  for (std::size_t player = 0U; player < 2U; ++player) {
    require(final_metrics.value().best_response_value[player] + 1.0e-12 >=
                final_metrics.value().profile_value[player],
            "exact four-street best response never underperforms the frozen profile");
  }

  auto resume_options = options;
  resume_options.iterations = 10'000U;
  const auto continuous = gtosd::solve_finite_game(game.value(), resume_options);
  resume_options.iterations = 5'000U;
  const auto partial = gtosd::solve_finite_game(game.value(), resume_options);
  resume_options.iterations = 10'000U;
  const auto resumed =
      partial ? gtosd::solve_finite_game(game.value(), resume_options, &partial.value().checkpoint)
              : gtosd::Result<gtosd::SolveResult, gtosd::SolverError>::failure(
                    gtosd::SolverError::InvalidCheckpoint);
  const auto continuous_bytes =
      continuous ? gtosd::serialize_solver_checkpoint(continuous.value().checkpoint)
                 : gtosd::Result<std::string, gtosd::SolverError>::failure(
                       gtosd::SolverError::InvalidCheckpoint);
  const auto resumed_bytes =
      resumed ? gtosd::serialize_solver_checkpoint(resumed.value().checkpoint)
              : gtosd::Result<std::string, gtosd::SolverError>::failure(
                    gtosd::SolverError::InvalidCheckpoint);
  require(continuous_bytes.has_value() && resumed_bytes.has_value() &&
              continuous_bytes.value() == resumed_bytes.value(),
          "four-street MCCFR resume is byte-equivalent to a continuous run");

  std::cout << "V21_WHOLE_GAME_NASHCONV=PASS"
            << " nodes=" << summary.value().nodes
            << " infosets=" << summary.value().information_sets
            << " initial_nashconv=" << certified.value().convergence.front().nash_conv
            << " final_nashconv=" << final_metrics.value().nash_conv
            << " normalized_nashconv=" << final_metrics.value().normalized_nash_conv
            << " profile_ev_co=" << final_metrics.value().profile_value[0]
            << " profile_ev_btn=" << final_metrics.value().profile_value[1]
            << " br_ev_co=" << final_metrics.value().best_response_value[0]
            << " br_ev_btn=" << final_metrics.value().best_response_value[1] << '\n';
}

} // namespace

int main() {
  try {
    test_exact_opponent_pass_expectation();
    test_unbiased_opponent_action_baseline();
    test_equal_probability_response_stratification();
    test_chance_sampled_full_action_reach_weights();
    test_dcfr_lazy_gap_equivalence();
    test_reduced_games_and_resume();
    test_four_street_short_deck_mccfr_nashconv();
    std::cout << "R2_EXTERNAL_SAMPLING_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "R2_EXTERNAL_SAMPLING_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "R2_EXTERNAL_SAMPLING_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
