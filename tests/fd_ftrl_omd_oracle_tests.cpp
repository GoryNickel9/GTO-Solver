#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Vector = std::vector<double>;

struct Node {
  std::size_t action_count{};
  std::vector<std::vector<std::size_t>> children;
};

using Tree = std::vector<Node>;
using NodeVectors = std::vector<Vector>;

std::size_t assertions = 0U;

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_close(const double actual, const double expected, const std::string &message,
                   const double tolerance = 1.0e-10) {
  require(std::abs(actual - expected) <= tolerance, message);
}

void require_vector_close(const Vector &actual, const Vector &expected,
                          const std::string &message, const double tolerance = 1.0e-10) {
  require(actual.size() == expected.size(), message + " (size)");
  for (std::size_t index = 0U; index < actual.size(); ++index) {
    require_close(actual[index], expected[index],
                  message + " (entry " + std::to_string(index) + ", actual=" +
                      std::to_string(actual[index]) + ", expected=" +
                      std::to_string(expected[index]) + ")",
                  tolerance);
  }
}

double sum_positive(const Vector &values) {
  return std::accumulate(values.begin(), values.end(), 0.0,
                         [](const double sum, const double value) {
                           return sum + std::max(0.0, value);
                         });
}

Vector normalize_positive(const Vector &values) {
  Vector result(values.size(), 0.0);
  const double sum = sum_positive(values);
  if (sum == 0.0) {
    std::fill(result.begin(), result.end(), 1.0 / static_cast<double>(values.size()));
    return result;
  }
  for (std::size_t action = 0U; action < values.size(); ++action) {
    result[action] = std::max(0.0, values[action]) / sum;
  }
  return result;
}

double dot(const Vector &left, const Vector &right) {
  require(left.size() == right.size(), "dot dimensions agree");
  return std::inner_product(left.begin(), left.end(), right.begin(), 0.0);
}

Vector instantaneous_regret(const Vector &strategy, const Vector &loss) {
  const double policy_loss = dot(strategy, loss);
  Vector result(loss.size(), 0.0);
  for (std::size_t action = 0U; action < loss.size(); ++action) {
    result[action] = policy_loss - loss[action];
  }
  return result;
}

void add_in_place(Vector &target, const Vector &increment) {
  require(target.size() == increment.size(), "vector addition dimensions agree");
  for (std::size_t index = 0U; index < target.size(); ++index) {
    target[index] += increment[index];
  }
}

NodeVectors zero_vectors(const Tree &tree) {
  NodeVectors result;
  result.reserve(tree.size());
  for (const auto &node : tree) {
    result.emplace_back(node.action_count, 0.0);
  }
  return result;
}

NodeVectors uniform_strategies(const Tree &tree) {
  auto result = zero_vectors(tree);
  for (std::size_t node = 0U; node < tree.size(); ++node) {
    std::fill(result[node].begin(), result[node].end(),
              1.0 / static_cast<double>(tree[node].action_count));
  }
  return result;
}

NodeVectors counterfactual_losses(const Tree &tree, const NodeVectors &base_loss,
                                  const NodeVectors &strategy) {
  auto result = base_loss;
  for (std::size_t reverse = tree.size(); reverse-- > 0U;) {
    for (std::size_t action = 0U; action < tree[reverse].action_count; ++action) {
      for (const auto child : tree[reverse].children[action]) {
        result[reverse][action] += dot(result[child], strategy[child]);
      }
    }
  }
  return result;
}

double solve_shift_l1(Vector thresholds, const double target) {
  require(!thresholds.empty(), "L1 solve has actions");
  require(target >= 0.0 && std::isfinite(target), "L1 target is valid");
  std::ranges::sort(thresholds);
  if (target == 0.0) {
    return thresholds.front();
  }
  double prefix = 0.0;
  for (std::size_t active = 1U; active <= thresholds.size(); ++active) {
    prefix += thresholds[active - 1U];
    const double alpha = (target + prefix) / static_cast<double>(active);
    if (active == thresholds.size() || alpha <= thresholds[active]) {
      return alpha;
    }
  }
  throw std::runtime_error("unreachable L1 solve");
}

double solve_shift_l2(Vector thresholds, const double target) {
  require(!thresholds.empty(), "L2 solve has actions");
  require(target >= 0.0 && std::isfinite(target), "L2 target is valid");
  std::ranges::sort(thresholds);
  if (target == 0.0) {
    return thresholds.front();
  }
  double prefix = 0.0;
  double square_prefix = 0.0;
  for (std::size_t active = 1U; active <= thresholds.size(); ++active) {
    const double threshold = thresholds[active - 1U];
    prefix += threshold;
    square_prefix += threshold * threshold;
    const double count = static_cast<double>(active);
    const double discriminant =
        std::max(0.0, prefix * prefix - count * (square_prefix - target));
    const double alpha = (prefix + std::sqrt(discriminant)) / count;
    if (active == thresholds.size() || alpha <= thresholds[active]) {
      return alpha;
    }
  }
  throw std::runtime_error("unreachable L2 solve");
}

struct FdUpdate {
  NodeVectors local_loss;
  NodeVectors state;
  NodeVectors strategy;
  Vector alpha;
};

FdUpdate fd_ftrl_cfr_update(const Tree &tree, const NodeVectors &cumulative_base_loss,
                            const Vector &beta) {
  FdUpdate result{cumulative_base_loss, zero_vectors(tree), zero_vectors(tree),
                  Vector(tree.size(), 0.0)};
  for (std::size_t reverse = tree.size(); reverse-- > 0U;) {
    for (std::size_t action = 0U; action < tree[reverse].action_count; ++action) {
      for (const auto child : tree[reverse].children[action]) {
        result.local_loss[reverse][action] += result.alpha[child];
      }
    }
    result.alpha[reverse] = solve_shift_l1(result.local_loss[reverse], beta[reverse]);
    for (std::size_t action = 0U; action < tree[reverse].action_count; ++action) {
      result.state[reverse][action] =
          result.alpha[reverse] - result.local_loss[reverse][action];
    }
    result.strategy[reverse] = normalize_positive(result.state[reverse]);
  }
  return result;
}

FdUpdate fd_omd_cfr_update(const Tree &tree, const NodeVectors &base_loss,
                           const NodeVectors &old_q, const Vector &new_beta) {
  FdUpdate result{base_loss, zero_vectors(tree), zero_vectors(tree), Vector(tree.size(), 0.0)};
  for (std::size_t reverse = tree.size(); reverse-- > 0U;) {
    for (std::size_t action = 0U; action < tree[reverse].action_count; ++action) {
      for (const auto child : tree[reverse].children[action]) {
        result.local_loss[reverse][action] += result.alpha[child];
      }
    }
    Vector thresholds(tree[reverse].action_count, 0.0);
    for (std::size_t action = 0U; action < tree[reverse].action_count; ++action) {
      thresholds[action] = result.local_loss[reverse][action] - old_q[reverse][action];
    }
    result.alpha[reverse] = solve_shift_l1(std::move(thresholds), new_beta[reverse]);
    for (std::size_t action = 0U; action < tree[reverse].action_count; ++action) {
      result.state[reverse][action] = std::max(
          0.0, old_q[reverse][action] + result.alpha[reverse] - result.local_loss[reverse][action]);
    }
    result.strategy[reverse] = normalize_positive(result.state[reverse]);
  }
  return result;
}

enum class LossCase : std::uint8_t {
  Zero,
  Constant,
  Alternating,
  Dominant,
  Tie,
  ZeroReach,
  NonUniformOpponentReach,
};

NodeVectors make_loss(const Tree &tree, const LossCase loss_case, const std::size_t iteration) {
  auto result = zero_vectors(tree);
  for (std::size_t node = 0U; node < tree.size(); ++node) {
    const double reach = loss_case == LossCase::ZeroReach
                             ? 0.0
                             : loss_case == LossCase::NonUniformOpponentReach
                                   ? (0.2 + 0.23 * static_cast<double>(node))
                                   : 1.0;
    for (std::size_t action = 0U; action < tree[node].action_count; ++action) {
      double loss = 0.0;
      switch (loss_case) {
      case LossCase::Zero:
      case LossCase::ZeroReach:
        loss = 0.0;
        break;
      case LossCase::Constant:
        loss = 1.75;
        break;
      case LossCase::Alternating:
        loss = static_cast<double>((iteration + action + node) % 2U);
        break;
      case LossCase::Dominant:
        loss = action == 0U ? -0.5 : 0.5 + static_cast<double>(action);
        break;
      case LossCase::Tie:
        loss = action < 2U ? 0.25 : 1.25;
        break;
      case LossCase::NonUniformOpponentReach:
        loss = static_cast<double>(action + 1U);
        break;
      }
      result[node][action] = reach * loss;
    }
  }
  return result;
}

Tree flat_tree(const std::size_t actions) {
  return {{actions, std::vector<std::vector<std::size_t>>(actions)}};
}

Tree branching_tree() {
  Tree tree;
  tree.push_back({3U, std::vector<std::vector<std::size_t>>(3U)});
  tree.push_back({2U, std::vector<std::vector<std::size_t>>(2U)});
  tree.push_back({4U, std::vector<std::vector<std::size_t>>(4U)});
  tree[0].children[0].push_back(1U);
  tree[0].children[2].push_back(2U);
  return tree;
}

void run_ftrl_equivalence(const Tree &tree, const LossCase loss_case) {
  auto regret = zero_vectors(tree);
  auto strategy = uniform_strategies(tree);
  auto cumulative_base = zero_vectors(tree);
  auto cumulative_counterfactual = zero_vectors(tree);
  for (std::size_t iteration = 0U; iteration < 12U; ++iteration) {
    const auto base = make_loss(tree, loss_case, iteration);
    const auto counterfactual = counterfactual_losses(tree, base, strategy);
    for (std::size_t node = 0U; node < tree.size(); ++node) {
      add_in_place(cumulative_base[node], base[node]);
      add_in_place(cumulative_counterfactual[node], counterfactual[node]);
      add_in_place(regret[node], instantaneous_regret(strategy[node], counterfactual[node]));
    }
    Vector beta(tree.size(), 0.0);
    for (std::size_t node = 0U; node < tree.size(); ++node) {
      beta[node] = sum_positive(regret[node]);
    }
    const auto fd = fd_ftrl_cfr_update(tree, cumulative_base, beta);
    for (std::size_t node = 0U; node < tree.size(); ++node) {
      require_vector_close(fd.local_loss[node], cumulative_counterfactual[node],
                           "FD-FTRL(CFR) reconstructs cumulative counterfactual loss");
      require_vector_close(fd.state[node], regret[node],
                           "FD-FTRL(CFR) reconstructs cumulative regret");
      require_vector_close(fd.strategy[node], normalize_positive(regret[node]),
                           "FD-FTRL(CFR) equals CFR-RM strategy");
    }
    strategy = fd.strategy;
  }
}

void run_omd_equivalence(const Tree &tree, const LossCase loss_case) {
  auto q = zero_vectors(tree);
  auto strategy = uniform_strategies(tree);
  for (std::size_t iteration = 0U; iteration < 12U; ++iteration) {
    const auto base = make_loss(tree, loss_case, iteration);
    const auto counterfactual = counterfactual_losses(tree, base, strategy);
    auto next_q = q;
    Vector beta(tree.size(), 0.0);
    for (std::size_t node = 0U; node < tree.size(); ++node) {
      add_in_place(next_q[node], instantaneous_regret(strategy[node], counterfactual[node]));
      for (double &value : next_q[node]) {
        value = std::max(0.0, value);
      }
      beta[node] = std::accumulate(next_q[node].begin(), next_q[node].end(), 0.0);
    }
    const auto fd = fd_omd_cfr_update(tree, base, q, beta);
    for (std::size_t node = 0U; node < tree.size(); ++node) {
      require_vector_close(fd.local_loss[node], counterfactual[node],
                           "FD-OMD(CFR) reconstructs instantaneous counterfactual loss");
      require_vector_close(fd.state[node], next_q[node],
                           "FD-OMD(CFR) reconstructs RM+ accumulator");
      require_vector_close(fd.strategy[node], normalize_positive(next_q[node]),
                           "FD-OMD(CFR) equals CFR-RM+ strategy");
    }
    q = std::move(next_q);
    strategy = fd.strategy;
  }
}

void test_equivalence_controls() {
  const std::vector<LossCase> cases{LossCase::Zero,
                                    LossCase::Constant,
                                    LossCase::Alternating,
                                    LossCase::Dominant,
                                    LossCase::Tie,
                                    LossCase::ZeroReach,
                                    LossCase::NonUniformOpponentReach};
  for (const std::size_t actions : {2U, 3U, 4U}) {
    const auto tree = flat_tree(actions);
    for (const auto loss_case : cases) {
      run_ftrl_equivalence(tree, loss_case);
      run_omd_equivalence(tree, loss_case);
    }
  }
  const auto tree = branching_tree();
  for (const auto loss_case : cases) {
    run_ftrl_equivalence(tree, loss_case);
    run_omd_equivalence(tree, loss_case);
  }
}

struct Quantized {
  Vector decoded;
  double scale{};
};

Quantized quantize_signed(const Vector &values) {
  const double maximum = std::accumulate(
      values.begin(), values.end(), 0.0,
      [](const double result, const double value) { return std::max(result, std::abs(value)); });
  const double scale = maximum > 0.0 ? maximum / 32767.0 : 0.0;
  Vector decoded(values.size(), 0.0);
  for (std::size_t index = 0U; index < values.size(); ++index) {
    const double code = scale > 0.0 ? std::nearbyint(values[index] / scale) : 0.0;
    decoded[index] = std::clamp(code, -32767.0, 32767.0) * scale;
  }
  return {std::move(decoded), scale};
}

Quantized quantize_unsigned(const Vector &values) {
  const double maximum = *std::ranges::max_element(values);
  const double scale = maximum > 0.0 ? maximum / 65535.0 : 0.0;
  Vector decoded(values.size(), 0.0);
  for (std::size_t index = 0U; index < values.size(); ++index) {
    const double code = scale > 0.0 ? std::nearbyint(values[index] / scale) : 0.0;
    decoded[index] = std::clamp(code, 0.0, 65535.0) * scale;
  }
  return {std::move(decoded), scale};
}

double l1_distance(const Vector &left, const Vector &right) {
  require(left.size() == right.size(), "L1 dimensions agree");
  double result = 0.0;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    result += std::abs(left[index] - right[index]);
  }
  return result;
}

void test_numerical_state_oracle() {
  for (const std::size_t actions : {2U, 3U, 4U}) {
    Vector signed_state(actions, 0.0);
    Vector unsigned_state(actions, 0.0);
    for (std::size_t action = 0U; action < actions; ++action) {
      signed_state[action] = (action % 2U == 0U ? 1.0 : -1.0) *
                             (0.125 + 0.375 * static_cast<double>(action));
      unsigned_state[action] = 0.2 + 0.35 * static_cast<double>(action);
    }
    const auto signed_quantized = quantize_signed(signed_state);
    const auto unsigned_quantized = quantize_unsigned(unsigned_state);
    for (std::size_t action = 0U; action < actions; ++action) {
      require(std::abs(signed_quantized.decoded[action] - signed_state[action]) <=
                  signed_quantized.scale * 0.500001,
              "signed i16 state obeys half-step error bound");
      require(std::abs(unsigned_quantized.decoded[action] - unsigned_state[action]) <=
                  unsigned_quantized.scale * 0.500001,
              "unsigned u16 state obeys half-step error bound");
    }
    require(l1_distance(normalize_positive(signed_quantized.decoded),
                        normalize_positive(signed_state)) < 1.0e-4,
            "FD-FTRL signed state preserves the local policy in the small oracle");
    require(l1_distance(normalize_positive(unsigned_quantized.decoded),
                        normalize_positive(unsigned_state)) < 1.0e-4,
            "FD-OMD nonnegative state preserves the local policy in the small oracle");

    Vector scaled = signed_state;
    for (double &value : scaled) {
      value *= 37.0;
    }
    require_vector_close(normalize_positive(scaled), normalize_positive(signed_state),
                         "positive common scaling leaves the decision map invariant");

    Vector q(actions, 1.0 / std::sqrt(static_cast<double>(actions)));
    Vector compressed_q = quantize_unsigned(q).decoded;
    for (std::size_t iteration = 1U; iteration <= 64U; ++iteration) {
      Vector loss(actions, 0.0);
      for (std::size_t action = 0U; action < actions; ++action) {
        loss[action] = 0.03 * static_cast<double>((iteration + 2U * action) % 7U);
      }
      const double lambda = 1.0 + 0.01 * static_cast<double>(iteration);
      Vector thresholds(actions, 0.0);
      Vector compressed_thresholds(actions, 0.0);
      for (std::size_t action = 0U; action < actions; ++action) {
        thresholds[action] = loss[action] - q[action];
        compressed_thresholds[action] = loss[action] - compressed_q[action];
      }
      const double alpha = solve_shift_l2(std::move(thresholds), lambda);
      const double compressed_alpha = solve_shift_l2(std::move(compressed_thresholds), lambda);
      for (std::size_t action = 0U; action < actions; ++action) {
        q[action] = std::max(0.0, q[action] + alpha - loss[action]);
        compressed_q[action] =
            std::max(0.0, compressed_q[action] + compressed_alpha - loss[action]);
      }
      compressed_q = quantize_unsigned(compressed_q).decoded;
      require(l1_distance(normalize_positive(q), normalize_positive(compressed_q)) < 1.0e-3,
              "FD-OMD u16 recurrence remains close in the bounded small oracle");
    }
  }
}

void test_zero_lambda_boundary() {
  const Vector thresholds{0.0, 1.0, 2.0};
  const double alpha = solve_shift_l2(thresholds, 0.0);
  Vector state(thresholds.size(), 0.0);
  for (std::size_t action = 0U; action < thresholds.size(); ++action) {
    state[action] = std::max(0.0, alpha - thresholds[action]);
  }
  require(sum_positive(state) == 0.0,
          "lambda zero produces no normalizable practical-R decision state");
  require(std::isfinite(alpha), "lambda-zero boundary solve remains finite");
}

} // namespace

int main() {
  try {
    test_equivalence_controls();
    test_numerical_state_oracle();
    test_zero_lambda_boundary();
    std::cout << "fd_ftrl_omd_oracle assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fd_ftrl_omd_oracle failure: " << error.what() << '\n';
    return 1;
  }
}
