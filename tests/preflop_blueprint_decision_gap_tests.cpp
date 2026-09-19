#include "gtosd/preflop_blueprint/decision_gap.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pb = gtosd::preflop_blueprint;
void require(const bool ok, const char *message) {
  if (!ok) {
    throw std::runtime_error(message);
  }
}
void near(const double a, const double b) {
  require(std::abs(a - b) <= 1e-12, "analytic gap mismatch");
}
int main() {
  try {
    // Two equally likely private signals. Opposite optimal actions: no common
    // action can exceed 1/2, while separating the signals achieves one.
    std::vector<pb::BucketDecisionSample> samples{{{1.0, 0.0}, 0.5, 1.0}, {{0.0, 1.0}, 0.5, 1.0}};
    const std::array<double, 2> uniform{0.5, 0.5};
    auto result = pb::bucket_decision_gap(samples, uniform);
    require(result.has_value(), "opposing preferences evaluate");
    near(result.value().mass, 1.0);
    near(result.value().shared_strategy_gain, 0.0);
    near(result.value().separation_gain, 0.5);
    // Common optimal action with a bad current policy: learning error only.
    samples[1].action_values = {1.0, 0.0};
    result = pb::bucket_decision_gap(samples, uniform);
    require(result.has_value(), "aligned preferences evaluate");
    near(result.value().shared_strategy_gain, 0.5);
    near(result.value().separation_gain, 0.0);
    // Reach is inside the values and the denominator exactly once.
    samples = {{{0.2, 0.0}, 0.75, 0.2}, {{0.0, 0.4}, 0.25, 0.4}};
    result = pb::bucket_decision_gap(samples, uniform);
    require(result.has_value(), "fractional reaches evaluate");
    near(result.value().mass, 0.25);
    near(result.value().shared_strategy_gain, 0.025);
    near(result.value().separation_gain, 0.1);
    // Adding a state-specific baseline to every action leaves both gaps fixed.
    samples[0].action_values[0] += 7;
    samples[0].action_values[1] += 7;
    samples[1].action_values[0] -= 3;
    samples[1].action_values[1] -= 3;
    result = pb::bucket_decision_gap(samples, uniform);
    require(result.has_value(), "translated payoffs evaluate");
    near(result.value().shared_strategy_gain, 0.025);
    near(result.value().separation_gain, 0.1);
    samples.clear();
    result = pb::bucket_decision_gap(samples, uniform);
    require(result.has_value(), "empty coverage is zero mass");
    near(result.value().mass, 0);
    near(result.value().separation_gain, 0);
    require(!pb::bucket_decision_gap(samples, std::array<double, 2>{0.2, 0.2}),
            "invalid probabilities rejected");
    samples = {{{1.0, 0.0}, -1.0, 1.0}};
    require(!pb::bucket_decision_gap(samples, uniform), "negative weights rejected");
    samples[0].weight = 1;
    samples[0].action_values[0] = std::numeric_limits<double>::quiet_NaN();
    require(!pb::bucket_decision_gap(samples, uniform), "NaN rejected");
    std::cout << "DECISION_GAP_TESTS=PASS opposing/aligned/fractional/translation/zero/invalid\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "DECISION_GAP_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
