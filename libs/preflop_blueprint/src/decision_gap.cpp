#include "gtosd/preflop_blueprint/decision_gap.hpp"

#include <algorithm>
#include <cmath>

namespace gtosd::preflop_blueprint {
namespace {
struct Sum {
  double value{0.0};
  double correction{0.0};
  void add(const double x) noexcept {
    const double y = x - correction;
    const double next = value + y;
    correction = (next - value) - y;
    value = next;
  }
};
} // namespace

Result<BucketDecisionGap, KernelError>
bucket_decision_gap(const std::span<const BucketDecisionSample> samples,
                    const std::span<const double> strategy) {
  using Outcome = Result<BucketDecisionGap, KernelError>;
  if (strategy.empty() || strategy.size() > maximum_actions) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  Sum probability;
  for (const auto value : strategy) {
    if (!std::isfinite(value) || value < 0.0) {
      return Outcome::failure(KernelError::InvalidInput);
    }
    probability.add(value);
  }
  if (std::abs(probability.value - 1.0) > 1e-12) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  std::array<Sum, maximum_actions> actions{};
  Sum mass;
  Sum separated;
  for (const auto &sample : samples) {
    if (!std::isfinite(sample.weight) || sample.weight < 0.0 ||
        !std::isfinite(sample.opponent_probability) || sample.opponent_probability < -1e-12 ||
        sample.opponent_probability > 1.0 + 1e-12) {
      return Outcome::failure(KernelError::InvalidInput);
    }
    double best = sample.action_values[0];
    for (std::size_t action = 0; action < strategy.size(); ++action) {
      const double value = sample.action_values[action];
      if (!std::isfinite(value)) {
        return Outcome::failure(KernelError::InvalidInput);
      }
      actions[action].add(sample.weight * value);
      best = std::max(best, value);
    }
    mass.add(sample.weight * sample.opponent_probability);
    separated.add(sample.weight * best);
  }
  BucketDecisionGap result;
  result.mass = mass.value;
  result.best_separated_value = separated.value;
  result.best_shared_value = actions[0].value;
  Sum current;
  for (std::size_t action = 0; action < strategy.size(); ++action) {
    current.add(strategy[action] * actions[action].value);
    if (actions[action].value > result.best_shared_value) {
      result.best_shared_value = actions[action].value;
      result.shared_action = action;
    }
  }
  result.strategy_value = current.value;
  result.shared_strategy_gain = result.best_shared_value - result.strategy_value;
  result.separation_gain = result.best_separated_value - result.best_shared_value;
  if (!std::isfinite(result.mass) || !std::isfinite(result.strategy_value) ||
      !std::isfinite(result.shared_strategy_gain) || !std::isfinite(result.separation_gain)) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  return Outcome::success(result);
}
} // namespace gtosd::preflop_blueprint
