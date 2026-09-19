#pragma once

#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include <array>
#include <span>

namespace gtosd::preflop_blueprint {

// One physically distinguishable state with hidden cards/runouts integrated.
// action_values include opponent reach; weight includes only the chosen hero
// and public-chance weighting. All samples share the same available actions.
struct BucketDecisionSample {
  std::array<double, maximum_actions> action_values{};
  double weight{0.0};
  double opponent_probability{0.0};
};

struct BucketDecisionGap {
  double mass{0.0};
  double strategy_value{0.0};
  double best_shared_value{0.0};
  double best_separated_value{0.0};
  double shared_strategy_gain{0.0};
  double separation_gain{0.0};
  std::size_t shared_action{0U};
};

// With S[a]=sum_s w[s] Q[s,a], pi a fixed bucket policy:
// shared_strategy_gain = max_a S[a] - sum_a pi[a] S[a]
// separation_gain     = sum_s w[s] max_a Q[s,a] - max_a S[a].
// Both are local, with the same frozen continuation and weights. They are NOT
// an abstract best response or an additive decomposition of root exploitability.
// Divide by mass only when mass > 0 to obtain the conditional local quantities.
[[nodiscard]] Result<BucketDecisionGap, KernelError>
bucket_decision_gap(std::span<const BucketDecisionSample> samples,
                    std::span<const double> shared_strategy);

} // namespace gtosd::preflop_blueprint
