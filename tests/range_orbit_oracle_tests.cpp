#include "gtosd/postflop/postflop_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::size_t assertions = 0U;

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void select_range_orbit_oracle(const bool enabled) {
#ifdef _WIN32
  _putenv_s("GTOSD_RANGE_ORBIT_ORACLE", enabled ? "1" : "");
#else
  if (enabled) {
    setenv("GTOSD_RANGE_ORBIT_ORACLE", "1", 1);
  } else {
    unsetenv("GTOSD_RANGE_ORBIT_ORACLE");
  }
#endif
}

struct EnvironmentReset {
  ~EnvironmentReset() { select_range_orbit_oracle(false); }
};

gtosd::PostflopTreeConfig make_config() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  config.flop = {gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value(),
                 gtosd::parse_card("Qh").value()};
  config.initial_pot = gtosd::Money::from_antes(40).value();
  config.effective_stack = gtosd::Money::from_antes(100).value();
  config.rake.enabled = true;
  config.rake.percentage = gtosd::RangeWeight::from_basis_points(0).value();
  config.rake.cap = gtosd::Money{};
  const auto half_pot = gtosd::PotPercentage::from_basis_points(5'000).value();
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      player[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = {half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes = {
          half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].aggressive_sizes = {
          half_pot};
      for (auto &scenario : player) {
        scenario.raise_depth = 0U;
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
      }
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].raise_depth = 1U;
    }
  }
  return config;
}

gtosd::PostflopRanges make_asymmetric_ranges() {
  gtosd::PostflopRanges ranges;
  const auto zero = gtosd::RangeWeight::from_basis_points(0).value();
  const auto full = gtosd::RangeWeight::from_basis_points(10'000).value();
  for (auto &range : ranges.players) {
    range.fill(zero);
  }
  constexpr std::array<std::string_view, 9> selected_classes{"AA",  "KK",  "QQ",  "AKs", "AQs",
                                                             "KQs", "AKo", "AQo", "KQo"};
  const auto combos = gtosd::all_combos();
  for (std::size_t combo = 0U; combo < combos.size(); ++combo) {
    const auto name = gtosd::class_name(gtosd::hand_class(combos[combo]));
    if (std::ranges::find(selected_classes, name) != selected_classes.end()) {
      ranges.players[0][combo] = full;
      ranges.players[1][combo] = full;
    }
    if (name == "AA") {
      ranges.players[1][combo] = zero;
    }
  }
  return ranges;
}

double maximum_difference(const std::vector<float> &baseline, const std::vector<float> &candidate) {
  require(baseline.size() == candidate.size(), "oracle state buffers have equal sizes");
  double result = 0.0;
  for (std::size_t index = 0U; index < baseline.size(); ++index) {
    result = std::max(result, std::abs(static_cast<double>(baseline[index]) -
                                       static_cast<double>(candidate[index])));
  }
  return result;
}

void test_asymmetric_range_physical_orbit_counterexample() {
  EnvironmentReset reset;
  const auto config = make_config();
  const auto ranges = make_asymmetric_ranges();
  require(ranges.players[0] != ranges.players[1], "oracle ranges are asymmetric");

  gtosd::PostflopSolveOptions options;
  options.iterations = 3U;
  options.averaging_delay = 0U;
  options.certification_interval = 3U;
  options.algorithm = gtosd::PostflopAlgorithm::DcfrPlus;
  options.state_precision = gtosd::PostflopStatePrecision::Float32;
  options.enable_lossless_isomorphism = true;
  options.enable_canonical_public_dag = false;

  select_range_orbit_oracle(false);
  const auto baseline = gtosd::solve_postflop_exact(config, ranges, options);
  require(baseline.has_value(), "physical baseline solve succeeds");
  select_range_orbit_oracle(true);
  const auto candidate = gtosd::solve_postflop_exact(config, ranges, options);
  require(candidate.has_value(), "range-orbit candidate solve succeeds");
  require(baseline.value().checkpoint.game_fingerprint ==
                  candidate.value().checkpoint.game_fingerprint &&
              baseline.value().actions == candidate.value().actions &&
              baseline.value().information_sets == candidate.value().information_sets,
          "oracle changes traversal reuse only, not game or layout identity");
  require(baseline.value().convergence.size() == 1U && candidate.value().convergence.size() == 1U,
          "baseline and candidate each produce one certification");

  const auto &baseline_certification = baseline.value().convergence.front();
  const auto &candidate_certification = candidate.value().convergence.front();
  double maximum_profile_difference = 0.0;
  double maximum_best_response_difference = 0.0;
  for (std::size_t player = 0U; player < 2U; ++player) {
    maximum_profile_difference = std::max(
        maximum_profile_difference, std::abs(baseline_certification.profile_value_antes[player] -
                                             candidate_certification.profile_value_antes[player]));
    maximum_best_response_difference =
        std::max(maximum_best_response_difference,
                 std::abs(baseline_certification.best_response_value_antes[player] -
                          candidate_certification.best_response_value_antes[player]));
  }
  const double maximum_regret_difference =
      maximum_difference(baseline.value().checkpoint.cumulative_regret_float32,
                         candidate.value().checkpoint.cumulative_regret_float32);
  const double maximum_strategy_difference =
      maximum_difference(baseline.value().checkpoint.cumulative_strategy_float32,
                         candidate.value().checkpoint.cumulative_strategy_float32);

  std::cout << "RANGE_ORBIT_COUNTEREXAMPLE_DIAGNOSTIC"
            << " maximum_profile_difference=" << maximum_profile_difference
            << " maximum_best_response_difference=" << maximum_best_response_difference
            << " maximum_regret_difference=" << maximum_regret_difference
            << " maximum_strategy_difference=" << maximum_strategy_difference << '\n';

  constexpr double exact_equivalence_tolerance = 1.0e-11;
  require(maximum_profile_difference > exact_equivalence_tolerance,
          "physical orbit reuse has a profile-EV counterexample");
  require(maximum_best_response_difference > exact_equivalence_tolerance,
          "physical orbit reuse has a best-response counterexample");
  require(maximum_regret_difference > exact_equivalence_tolerance,
          "physical orbit reuse changes cumulative regret state");
  require(maximum_strategy_difference > exact_equivalence_tolerance,
          "physical orbit reuse changes cumulative strategy state");

  std::cout << "RANGE_ORBIT_COUNTEREXAMPLE_TEST=PASS assertions=" << assertions << '\n';
}

} // namespace

int main() {
  try {
    test_asymmetric_range_physical_orbit_counterexample();
    return 0;
  } catch (const std::exception &error) {
    select_range_orbit_oracle(false);
    std::cerr << "RANGE_ORBIT_COUNTEREXAMPLE_TEST=FAIL assertions=" << assertions
              << " error=" << error.what() << '\n';
    return 1;
  }
}
