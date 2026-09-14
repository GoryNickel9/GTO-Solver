#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gtosd {

// In the standard two-pass heads-up external-sampling traversal, average
// strategy is accumulated only while the actor is the opponent of the
// traverser. The probability of visiting that node already contains the
// actor's own reach, so multiplying by that reach a second time is biased.
//
// A one-sided response trainer has no opponent pass for the responding
// player. Its traverser nodes are enumerated rather than sampled, so their
// own reach must be supplied explicitly.
enum class ExternalSamplingAveragePass : std::uint8_t { OpponentOfTraverser, OneSidedTraverser };

// With chance sampled from the game's true distribution and every player
// action enumerated, regret carries the opponent reach while average strategy
// carries the acting player's reach. Chance reach is already represented by
// the sampling probability.
[[nodiscard]] constexpr double
chance_sampled_cfr_regret_delta(const double action_value, const double node_value,
                                const double counterfactual_opponent_reach) noexcept {
  return counterfactual_opponent_reach * (action_value - node_value);
}

[[nodiscard]] constexpr double
chance_sampled_cfr_average_multiplier(const double acting_player_reach) noexcept {
  return acting_player_reach;
}

[[nodiscard]] constexpr double external_sampling_average_multiplier(
    const std::uint8_t actor, const std::uint8_t traverser, const double actor_reach,
    const ExternalSamplingAveragePass pass =
        ExternalSamplingAveragePass::OpponentOfTraverser) noexcept {
  if (pass == ExternalSamplingAveragePass::OpponentOfTraverser) {
    return actor == traverser ? 0.0 : 1.0;
  }
  return actor == traverser ? actor_reach : 0.0;
}

// Maps one uniform draw inside an equal-width stratum back to [0, 1). Averaging
// one sample from every stratum preserves the original uniform expectation.
// Callers validate the indices at their configuration boundary.
[[nodiscard]] constexpr double external_sampling_stratified_quantile(
    const std::size_t stratum, const std::size_t stratum_count,
    const double within_stratum_draw) noexcept {
  return (static_cast<double>(stratum) + within_stratum_draw) /
         static_cast<double>(stratum_count);
}

template <std::size_t Capacity>
[[nodiscard]] constexpr std::size_t external_sampling_action_from_quantile(
    const std::array<double, Capacity> &strategy, const std::size_t action_count,
    const double quantile) noexcept {
  double cumulative = 0.0;
  for (std::size_t action = 0U; action < action_count; ++action) {
    cumulative += strategy[action];
    if (quantile < cumulative || action + 1U == action_count) {
      return action;
    }
  }
  return action_count - 1U;
}

// Unbiased control-variate estimator for one sampled opponent action. The
// sampling distribution is the current strategy, so its probability cancels
// the matching reach weight in the node expectation. Baselines must be read
// before observing sampled_child_value.
template <std::size_t Capacity>
[[nodiscard]] constexpr double external_sampling_opponent_baseline_estimate(
    const std::array<double, Capacity> &strategy, const std::array<double, Capacity> &baselines,
    const std::size_t action_count, const std::size_t sampled_action,
    const double sampled_child_value) noexcept {
  double expectation = 0.0;
  for (std::size_t action = 0U; action < action_count; ++action) {
    expectation += strategy[action] * baselines[action];
  }
  return expectation + sampled_child_value - baselines[sampled_action];
}

struct Dcfr1503LazyDiscount {
  double positive_regret{1.0};
  double negative_regret{1.0};
  double average_strategy{1.0};
};

// Converts the elapsed global-iteration interval into the same multipliers as
// applying DCFR 1.5/0/3 at every skipped iteration. positive_log_delta is the
// sum of log(t^1.5 / (t^1.5 + 1)) over (last_iteration, iteration].
[[nodiscard]] inline Dcfr1503LazyDiscount
dcfr_1503_lazy_discount(const std::uint64_t last_iteration, const std::uint64_t iteration,
                        const double positive_log_delta) noexcept {
  if (iteration <= last_iteration) {
    return {};
  }
  const auto elapsed = static_cast<double>(iteration - last_iteration);
  const auto strategy_ratio =
      static_cast<double>(last_iteration + 1U) / static_cast<double>(iteration + 1U);
  return {std::exp(positive_log_delta), std::pow(0.5, elapsed),
          strategy_ratio * strategy_ratio * strategy_ratio};
}

} // namespace gtosd
