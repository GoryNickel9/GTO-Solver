#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Vector = std::vector<double>;

std::size_t assertions = 0U;

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_close(const double actual, const double expected, const std::string &message) {
  require(std::abs(actual - expected) <= 1.0e-12, message);
}

void require_vector_close(const Vector &actual, const Vector &expected,
                          const std::string &message) {
  require(actual.size() == expected.size(), message + " (size)");
  for (std::size_t index = 0U; index < actual.size(); ++index) {
    require_close(actual[index], expected[index], message + " (entry)");
  }
}

Vector normalize_positive(const Vector &weights) {
  Vector strategy(weights.size(), 0.0);
  const double sum = std::accumulate(weights.begin(), weights.end(), 0.0,
                                     [](const double total, const double value) {
                                       return total + std::max(0.0, value);
                                     });
  if (sum == 0.0) {
    std::fill(strategy.begin(), strategy.end(), 1.0 / static_cast<double>(weights.size()));
    return strategy;
  }
  for (std::size_t action = 0U; action < weights.size(); ++action) {
    strategy[action] = std::max(0.0, weights[action]) / sum;
  }
  return strategy;
}

Vector instantaneous_regret(const Vector &strategy, const Vector &loss) {
  require(strategy.size() == loss.size(), "strategy and loss dimensions agree");
  const double policy_loss =
      std::inner_product(strategy.begin(), strategy.end(), loss.begin(), 0.0);
  Vector result(loss.size(), 0.0);
  for (std::size_t action = 0U; action < loss.size(); ++action) {
    result[action] = policy_loss - loss[action];
  }
  return result;
}

Vector clipped_add(const Vector &left, const Vector &right, const double left_scale = 1.0) {
  require(left.size() == right.size(), "regret vectors have equal dimensions");
  Vector result(left.size(), 0.0);
  for (std::size_t action = 0U; action < left.size(); ++action) {
    result[action] = std::max(0.0, left_scale * left[action] + right[action]);
  }
  return result;
}

struct PredictiveStep {
  Vector cumulative_regret;
  Vector predicted_regret;
  Vector next_strategy;
};

PredictiveStep pcfr_plus_step(const Vector &old_cumulative_regret, const Vector &played_strategy,
                              const Vector &observed_loss, const Vector &next_loss_prediction) {
  const auto observed_regret = instantaneous_regret(played_strategy, observed_loss);
  auto cumulative_regret = clipped_add(old_cumulative_regret, observed_regret);
  const auto prediction = instantaneous_regret(played_strategy, next_loss_prediction);
  auto predicted_regret = clipped_add(cumulative_regret, prediction);
  auto next_strategy = normalize_positive(predicted_regret);
  return {std::move(cumulative_regret), std::move(predicted_regret), std::move(next_strategy)};
}

PredictiveStep pdcfr_plus_step(const Vector &old_cumulative_regret,
                               const Vector &played_strategy, const Vector &observed_loss,
                               const Vector &next_loss_prediction, const double current_discount,
                               const double next_discount) {
  const auto observed_regret = instantaneous_regret(played_strategy, observed_loss);
  auto cumulative_regret = clipped_add(old_cumulative_regret, observed_regret, current_discount);
  const auto prediction = instantaneous_regret(played_strategy, next_loss_prediction);
  auto predicted_regret = clipped_add(cumulative_regret, prediction, next_discount);
  auto next_strategy = normalize_positive(predicted_regret);
  return {std::move(cumulative_regret), std::move(predicted_regret), std::move(next_strategy)};
}

void test_two_action_pcfr_plus_formula() {
  const auto result = pcfr_plus_step({1.0, 0.0}, {0.25, 0.75}, {2.0, 0.0}, {2.0, 0.0});
  require_vector_close(result.cumulative_regret, {0.0, 0.5},
                       "PCFR+ clips the observed cumulative regret");
  require_vector_close(result.predicted_regret, {0.0, 1.0},
                       "PCFR+ adds the predicted instantaneous regret");
  require_vector_close(result.next_strategy, {0.0, 1.0},
                       "PCFR+ normalizes predicted positive regret");
}

void test_three_action_and_zero_regret() {
  const auto result = pcfr_plus_step({0.0, 0.0, 0.0}, {0.2, 0.3, 0.5}, {3.0, 0.0, 1.0},
                                     {3.0, 0.0, 1.0});
  require_vector_close(result.cumulative_regret, {0.0, 1.1, 0.1},
                       "three-action cumulative regret is exact");
  require_vector_close(result.predicted_regret, {0.0, 2.2, 0.2},
                       "three-action predicted regret is exact");
  require_vector_close(result.next_strategy, {0.0, 11.0 / 12.0, 1.0 / 12.0},
                       "three-action prediction is normalized");

  const auto zero = pcfr_plus_step({0.0, 0.0, 0.0}, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0},
                                   {4.0, 4.0, 4.0}, {4.0, 4.0, 4.0});
  require_vector_close(zero.cumulative_regret, {0.0, 0.0, 0.0},
                       "constant utilities produce zero regret");
  require_vector_close(zero.next_strategy, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0},
                       "zero predicted regret uses the uniform policy");
}

void test_prediction_quality_and_degenerate_predictor() {
  const Vector old_regret{0.5, 0.5};
  const Vector strategy{0.5, 0.5};
  const Vector loss{0.0, 2.0};
  const auto exact = pcfr_plus_step(old_regret, strategy, loss, loss);
  const auto inaccurate = pcfr_plus_step(old_regret, strategy, loss, {2.0, 0.0});
  require(exact.next_strategy[0] > inaccurate.next_strategy[0],
          "an inaccurate predictor changes the optimistic policy in the expected direction");

  const auto zero_prediction = pcfr_plus_step(old_regret, strategy, loss, {0.0, 0.0});
  require_vector_close(zero_prediction.predicted_regret, zero_prediction.cumulative_regret,
                       "a constant zero loss prediction reduces to RM+ strategy selection");
  require_vector_close(zero_prediction.next_strategy,
                       normalize_positive(zero_prediction.cumulative_regret),
                       "the degenerate predictor matches the RM+ policy");
}

void test_oscillating_losses_are_finite() {
  Vector cumulative{0.0, 0.0};
  Vector strategy{0.5, 0.5};
  Vector previous_loss{0.0, 0.0};
  for (std::size_t iteration = 0U; iteration < 16U; ++iteration) {
    const Vector loss = iteration % 2U == 0U ? Vector{0.0, 1.0} : Vector{1.0, 0.0};
    const auto result = pcfr_plus_step(cumulative, strategy, loss, previous_loss);
    cumulative = result.cumulative_regret;
    strategy = result.next_strategy;
    previous_loss = loss;
    require(std::ranges::all_of(cumulative,
                                [](const double value) { return std::isfinite(value); }),
            "oscillating cumulative regrets remain finite");
    require_close(std::accumulate(strategy.begin(), strategy.end(), 0.0), 1.0,
                  "oscillating predictive policy remains normalized");
  }
}

void test_pdcfr_plus_formula() {
  const auto result =
      pdcfr_plus_step({4.0, 2.0}, {0.5, 0.5}, {0.0, 2.0}, {0.0, 2.0}, 0.5, 0.75);
  require_vector_close(result.cumulative_regret, {3.0, 0.0},
                       "PDCFR+ discounts then clips cumulative regret");
  require_vector_close(result.predicted_regret, {3.25, 0.0},
                       "PDCFR+ discounts cumulative regret before prediction");
  require_vector_close(result.next_strategy, {1.0, 0.0},
                       "PDCFR+ normalizes predicted cumulative regret");
}

} // namespace

int main() {
  try {
    test_two_action_pcfr_plus_formula();
    test_three_action_and_zero_regret();
    test_prediction_quality_and_degenerate_predictor();
    test_oscillating_losses_are_finite();
    test_pdcfr_plus_formula();
    std::cout << "predictive_cfr_oracle assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "predictive_cfr_oracle failure: " << error.what() << '\n';
    return 1;
  }
}
