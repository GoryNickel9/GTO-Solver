#include "gtosd/solver/solver.hpp"

#include "gtosd/core/external_sampling.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace gtosd {
namespace {

using NumericMap = std::map<std::string, std::vector<double>>;

struct IterationDelta {
  IterationDelta() = default;
  IterationDelta(const IterationDelta &) = delete;
  IterationDelta &operator=(const IterationDelta &) = delete;
  IterationDelta(IterationDelta &&) = delete;
  IterationDelta &operator=(IterationDelta &&) = delete;
  ~IterationDelta() = default;

  NumericMap regret;
  NumericMap strategy;
  std::uint64_t traversed_nodes{0};
};

bool finite_vector(const std::vector<double> &values) {
  return std::ranges::all_of(values, [](const double value) { return std::isfinite(value); });
}

std::vector<double> regret_matching(const InformationSetBuffer &buffer) {
  std::vector<double> strategy(buffer.actions.size(), 0.0);
  double positive_sum = 0.0;
  for (const double regret : buffer.cumulative_regret) {
    positive_sum += std::max(0.0, regret);
  }
  if (positive_sum <= 0.0) {
    const double probability = 1.0 / static_cast<double>(strategy.size());
    std::ranges::fill(strategy, probability);
    return strategy;
  }
  for (std::size_t index = 0; index < strategy.size(); ++index) {
    strategy[index] = std::max(0.0, buffer.cumulative_regret[index]) / positive_sum;
  }
  return strategy;
}

Result<std::map<std::string, InformationSetBuffer>, SolverError>
make_information_set_buffers(const FiniteGame &game) {
  const auto uniform = uniform_strategy_profile(game);
  if (!uniform) {
    return Result<std::map<std::string, InformationSetBuffer>, SolverError>::failure(
        uniform.error());
  }
  std::map<std::string, InformationSetBuffer> buffers;
  for (const auto &[key, strategy] : uniform.value()) {
    InformationSetBuffer buffer;
    buffer.player = strategy.player;
    buffer.actions = strategy.actions;
    buffer.cumulative_regret.resize(strategy.actions.size(), 0.0);
    buffer.cumulative_strategy.resize(strategy.actions.size(), 0.0);
    buffers.emplace(key, std::move(buffer));
  }
  return Result<std::map<std::string, InformationSetBuffer>, SolverError>::success(
      std::move(buffers));
}

StrategyProfile strategy_from_buffers(const std::map<std::string, InformationSetBuffer> &buffers,
                                      const bool average) {
  StrategyProfile profile;
  for (const auto &[key, buffer] : buffers) {
    InformationSetStrategy strategy;
    strategy.player = buffer.player;
    strategy.actions = buffer.actions;
    if (average) {
      strategy.probabilities = buffer.cumulative_strategy;
      double sum = 0.0;
      for (const double value : strategy.probabilities) {
        sum += std::max(0.0, value);
      }
      if (sum > 0.0) {
        for (double &value : strategy.probabilities) {
          value = std::max(0.0, value) / sum;
        }
      } else {
        strategy.probabilities = regret_matching(buffer);
      }
    } else {
      strategy.probabilities = regret_matching(buffer);
    }
    profile.emplace(key, std::move(strategy));
  }
  return profile;
}

std::array<double, 2> traverse_full(const FiniteGame &game, const GameNodeId node_id,
                                    const StrategyProfile &strategy,
                                    const std::array<double, 2> reach, const double chance_reach,
                                    IterationDelta &delta) {
  ++delta.traversed_nodes;
  const auto &node = game.nodes[node_id];
  if (node.kind == GameNodeKind::Terminal) {
    return node.payoff;
  }
  if (node.kind == GameNodeKind::Chance) {
    std::array<double, 2> value{0.0, 0.0};
    for (const auto &edge : node.edges) {
      const auto child =
          traverse_full(game, edge.child, strategy, reach, chance_reach * edge.probability, delta);
      value[0] += edge.probability * child[0];
      value[1] += edge.probability * child[1];
    }
    return value;
  }

  const auto &information_strategy = strategy.at(node.information_set);
  std::vector<std::array<double, 2>> action_values;
  action_values.reserve(node.edges.size());
  std::array<double, 2> value{0.0, 0.0};
  for (std::size_t index = 0; index < node.edges.size(); ++index) {
    auto child_reach = reach;
    child_reach[node.player] *= information_strategy.probabilities[index];
    const auto child =
        traverse_full(game, node.edges[index].child, strategy, child_reach, chance_reach, delta);
    action_values.push_back(child);
    value[0] += information_strategy.probabilities[index] * child[0];
    value[1] += information_strategy.probabilities[index] * child[1];
  }

  auto &regret_delta = delta.regret[node.information_set];
  auto &strategy_delta = delta.strategy[node.information_set];
  if (regret_delta.empty()) {
    regret_delta.resize(node.edges.size(), 0.0);
    strategy_delta.resize(node.edges.size(), 0.0);
  }
  const std::uint8_t opponent = static_cast<std::uint8_t>(1U - node.player);
  const double counterfactual_weight = reach[opponent] * chance_reach;
  const double average_weight = reach[node.player] * chance_reach;
  for (std::size_t index = 0; index < node.edges.size(); ++index) {
    regret_delta[index] +=
        counterfactual_weight * (action_values[index][node.player] - value[node.player]);
    strategy_delta[index] += average_weight * information_strategy.probabilities[index];
  }
  return value;
}

void merge_numeric_map(NumericMap &destination, const NumericMap &source) {
  for (const auto &[key, values] : source) {
    auto &target = destination[key];
    if (target.empty()) {
      target.resize(values.size(), 0.0);
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      target[index] += values[index];
    }
  }
}

void traverse_full_iteration(const FiniteGame &game, const StrategyProfile &strategy,
                             IterationDelta &delta) {
  static_cast<void>(traverse_full(game, game.root, strategy, {1.0, 1.0}, 1.0, delta));
}

double traverse_alternating(const FiniteGame &game, const GameNodeId node_id,
                            const StrategyProfile &strategy, const std::uint8_t updating_player,
                            const std::array<double, 2> reach, const double chance_reach,
                            IterationDelta &delta) {
  ++delta.traversed_nodes;
  const auto &node = game.nodes[node_id];
  if (node.kind == GameNodeKind::Terminal) {
    return node.payoff[updating_player];
  }
  if (node.kind == GameNodeKind::Chance) {
    double value = 0.0;
    for (const auto &edge : node.edges) {
      value +=
          edge.probability * traverse_alternating(game, edge.child, strategy, updating_player,
                                                  reach, chance_reach * edge.probability, delta);
    }
    return value;
  }

  const auto &information_strategy = strategy.at(node.information_set);
  std::vector<double> action_values(node.edges.size(), 0.0);
  double value = 0.0;
  for (std::size_t index = 0U; index < node.edges.size(); ++index) {
    auto child_reach = reach;
    child_reach[node.player] *= information_strategy.probabilities[index];
    action_values[index] = traverse_alternating(game, node.edges[index].child, strategy,
                                                updating_player, child_reach, chance_reach, delta);
    value += information_strategy.probabilities[index] * action_values[index];
  }
  if (node.player != updating_player) {
    return value;
  }

  auto &regret_delta = delta.regret[node.information_set];
  auto &strategy_delta = delta.strategy[node.information_set];
  if (regret_delta.empty()) {
    regret_delta.resize(node.edges.size(), 0.0);
    strategy_delta.resize(node.edges.size(), 0.0);
  }
  const auto opponent = static_cast<std::uint8_t>(1U - node.player);
  const double counterfactual_weight = reach[opponent] * chance_reach;
  const double average_weight = reach[node.player] * chance_reach;
  for (std::size_t index = 0U; index < node.edges.size(); ++index) {
    regret_delta[index] += counterfactual_weight * (action_values[index] - value);
    strategy_delta[index] += average_weight * information_strategy.probabilities[index];
  }
  return value;
}

class ExactTraversalWorkers {
public:
  ExactTraversalWorkers(const FiniteGame &game, const std::uint32_t requested_threads)
      : game_(game),
        worker_count_(std::min<std::size_t>(requested_threads, game.nodes[game.root].edges.size())),
        start_barrier_(static_cast<std::ptrdiff_t>(worker_count_ + 1U)),
        finish_barrier_(static_cast<std::ptrdiff_t>(worker_count_ + 1U)) {
    worker_deltas_.reserve(worker_count_);
    for (std::size_t worker = 0; worker < worker_count_; ++worker) {
      worker_deltas_.push_back(std::make_unique<IterationDelta>());
    }
    workers_.reserve(worker_count_);
    for (std::size_t worker = 0; worker < worker_count_; ++worker) {
      workers_.emplace_back([this, worker]() { worker_loop(worker); });
    }
  }

  ExactTraversalWorkers(const ExactTraversalWorkers &) = delete;
  ExactTraversalWorkers &operator=(const ExactTraversalWorkers &) = delete;

  ~ExactTraversalWorkers() {
    stopping_.store(true);
    start_barrier_.arrive_and_wait();
    for (auto &worker : workers_) {
      worker.join();
    }
  }

  void traverse(const StrategyProfile &strategy, IterationDelta &combined) {
    strategy_ = &strategy;
    start_barrier_.arrive_and_wait();
    finish_barrier_.arrive_and_wait();

    combined.traversed_nodes = 1U;
    for (const auto &delta : worker_deltas_) {
      merge_numeric_map(combined.regret, delta->regret);
      merge_numeric_map(combined.strategy, delta->strategy);
      combined.traversed_nodes += delta->traversed_nodes;
    }
  }

private:
  void worker_loop(const std::size_t worker) {
    while (true) {
      start_barrier_.arrive_and_wait();
      if (stopping_.load()) {
        return;
      }
      auto &delta = *worker_deltas_[worker];
      delta.regret.clear();
      delta.strategy.clear();
      delta.traversed_nodes = 0;
      const auto &root = game_.nodes[game_.root];
      for (std::size_t edge_index = worker; edge_index < root.edges.size();
           edge_index += worker_count_) {
        const auto &root_edge = root.edges[edge_index];
        static_cast<void>(traverse_full(game_, root_edge.child, *strategy_, {1.0, 1.0},
                                        root_edge.probability, delta));
      }
      finish_barrier_.arrive_and_wait();
    }
  }

  const FiniteGame &game_;
  std::size_t worker_count_;
  std::barrier<> start_barrier_;
  std::barrier<> finish_barrier_;
  std::vector<std::unique_ptr<IterationDelta>> worker_deltas_;
  std::vector<std::thread> workers_;
  const StrategyProfile *strategy_{nullptr};
  std::atomic<bool> stopping_{false};
};

bool can_parallelize_root(const FiniteGame &game, const SolverConfig &config) {
  return config.thread_count > 1U && game.nodes[game.root].kind == GameNodeKind::Chance &&
         game.nodes[game.root].edges.size() > 1U &&
         config.algorithm != SolverAlgorithm::ExternalSamplingMccfr;
}

std::uint64_t next_random(std::uint64_t &state) {
  state += 0x9e3779b97f4a7c15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

double uniform_unit(std::uint64_t &state) {
  constexpr double scale = 1.0 / static_cast<double>(std::uint64_t{1} << 53U);
  return static_cast<double>(next_random(state) >> 11U) * scale;
}

std::size_t sample_probabilities(const std::vector<double> &probabilities, std::uint64_t &state) {
  const double target = uniform_unit(state);
  double cumulative = 0.0;
  for (std::size_t index = 0; index < probabilities.size(); ++index) {
    cumulative += probabilities[index];
    if (target < cumulative || index + 1U == probabilities.size()) {
      return index;
    }
  }
  return probabilities.size() - 1U;
}

double traverse_external_sampling(const FiniteGame &game, const GameNodeId node_id,
                                  const StrategyProfile &strategy,
                                  const std::uint8_t updating_player,
                                  const std::array<double, 2> reach, IterationDelta &delta,
                                  std::uint64_t &rng_state) {
  ++delta.traversed_nodes;
  const auto &node = game.nodes[node_id];
  if (node.kind == GameNodeKind::Terminal) {
    return node.payoff[updating_player];
  }
  if (node.kind == GameNodeKind::Chance) {
    std::vector<double> probabilities;
    probabilities.reserve(node.edges.size());
    for (const auto &edge : node.edges) {
      probabilities.push_back(edge.probability);
    }
    const std::size_t sampled = sample_probabilities(probabilities, rng_state);
    return traverse_external_sampling(game, node.edges[sampled].child, strategy, updating_player,
                                      reach, delta, rng_state);
  }

  const auto &information_strategy = strategy.at(node.information_set);
  const double average_multiplier =
      external_sampling_average_multiplier(node.player, updating_player, reach[node.player]);
  if (average_multiplier != 0.0) {
    auto &strategy_delta = delta.strategy[node.information_set];
    if (strategy_delta.empty()) {
      strategy_delta.resize(node.edges.size(), 0.0);
    }
    for (std::size_t index = 0; index < node.edges.size(); ++index) {
      strategy_delta[index] += average_multiplier * information_strategy.probabilities[index];
    }
  }

  if (node.player != updating_player) {
    const std::size_t sampled = sample_probabilities(information_strategy.probabilities, rng_state);
    auto child_reach = reach;
    child_reach[node.player] *= information_strategy.probabilities[sampled];
    return traverse_external_sampling(game, node.edges[sampled].child, strategy, updating_player,
                                      child_reach, delta, rng_state);
  }

  std::vector<double> action_values(node.edges.size(), 0.0);
  double value = 0.0;
  for (std::size_t index = 0; index < node.edges.size(); ++index) {
    auto child_reach = reach;
    child_reach[node.player] *= information_strategy.probabilities[index];
    action_values[index] = traverse_external_sampling(
        game, node.edges[index].child, strategy, updating_player, child_reach, delta, rng_state);
    value += information_strategy.probabilities[index] * action_values[index];
  }
  auto &regret_delta = delta.regret[node.information_set];
  if (regret_delta.empty()) {
    regret_delta.resize(node.edges.size(), 0.0);
  }
  for (std::size_t index = 0; index < node.edges.size(); ++index) {
    regret_delta[index] += action_values[index] - value;
  }
  return value;
}

double power_discount(const double numerator, const double exponent) {
  if (numerator <= 0.0 && exponent < 0.0) {
    return 0.0;
  }
  const double powered = std::pow(numerator, exponent);
  return powered / (powered + 1.0);
}

bool compatible_resume(const SolverCheckpoint &checkpoint, const FiniteGame &game,
                       const SolverConfig &config) {
  const auto &stored = checkpoint.config;
  return checkpoint.major == SolverCheckpoint::format_major &&
         checkpoint.minor <= SolverCheckpoint::format_minor &&
         checkpoint.game_fingerprint == finite_game_fingerprint(game) &&
         stored.algorithm == config.algorithm && stored.seed == config.seed &&
         stored.thread_count == config.thread_count &&
         stored.averaging_delay == config.averaging_delay &&
         stored.dcfr.positive_regret_exponent == config.dcfr.positive_regret_exponent &&
         stored.dcfr.negative_regret_exponent == config.dcfr.negative_regret_exponent &&
         stored.dcfr.strategy_exponent == config.dcfr.strategy_exponent;
}

bool valid_configuration(const SolverConfig &config) {
  const bool supported_threads = config.thread_count == 1U || config.thread_count == 2U ||
                                 config.thread_count == 4U || config.thread_count == 8U;
  const bool sampled = config.algorithm == SolverAlgorithm::ExternalSamplingMccfr ||
                       config.algorithm == SolverAlgorithm::LinearMccfr;
  const bool supported_sampling_threads = !sampled || config.thread_count == 1U;
  const bool production_contract =
      config.algorithm != SolverAlgorithm::ProductionDcfr ||
      (config.thread_count == 1U && config.averaging_delay == 0U &&
       config.dcfr.positive_regret_exponent == 1.5 && config.dcfr.negative_regret_exponent == 0.0 &&
       config.dcfr.strategy_exponent == 3.0);
  return config.iterations > 0U && supported_threads && supported_sampling_threads &&
         production_contract && std::isfinite(config.dcfr.positive_regret_exponent) &&
         std::isfinite(config.dcfr.negative_regret_exponent) &&
         std::isfinite(config.dcfr.strategy_exponent);
}

void apply_iteration_delta(SolverCheckpoint &checkpoint, const IterationDelta &delta,
                           const std::uint64_t iteration) {
  const auto algorithm = checkpoint.config.algorithm;
  const double iteration_value = static_cast<double>(iteration);
  for (auto &[key, buffer] : checkpoint.information_sets) {
    const auto regret_found = delta.regret.find(key);
    const auto strategy_found = delta.strategy.find(key);
    const std::vector<double> zero(buffer.actions.size(), 0.0);
    const auto &regret_delta = regret_found == delta.regret.end() ? zero : regret_found->second;
    const auto &strategy_delta =
        strategy_found == delta.strategy.end() ? zero : strategy_found->second;

    if (algorithm == SolverAlgorithm::Dcfr) {
      const double positive_discount =
          power_discount(iteration_value, checkpoint.config.dcfr.positive_regret_exponent);
      const double negative_discount =
          power_discount(iteration_value, checkpoint.config.dcfr.negative_regret_exponent);
      const double strategy_discount = std::pow(iteration_value / (iteration_value + 1.0),
                                                checkpoint.config.dcfr.strategy_exponent);
      for (std::size_t index = 0; index < buffer.actions.size(); ++index) {
        buffer.cumulative_regret[index] *=
            buffer.cumulative_regret[index] >= 0.0 ? positive_discount : negative_discount;
        buffer.cumulative_strategy[index] *= strategy_discount;
      }
    }

    const bool linear =
        algorithm == SolverAlgorithm::LinearCfr || algorithm == SolverAlgorithm::LinearMccfr;
    const double regret_weight = linear ? iteration_value : 1.0;
    double strategy_weight = 1.0;
    if (linear) {
      strategy_weight = iteration_value;
    } else if (algorithm == SolverAlgorithm::CfrPlus) {
      strategy_weight = iteration > checkpoint.config.averaging_delay
                            ? static_cast<double>(iteration - checkpoint.config.averaging_delay)
                            : 0.0;
    }
    for (std::size_t index = 0; index < buffer.actions.size(); ++index) {
      const double updated = buffer.cumulative_regret[index] + regret_weight * regret_delta[index];
      buffer.cumulative_regret[index] =
          algorithm == SolverAlgorithm::CfrPlus ? std::max(0.0, updated) : updated;
      buffer.cumulative_strategy[index] += strategy_weight * strategy_delta[index];
    }
  }
}

struct FiniteProductionSchedulePoint {
  std::uint64_t regret_discount_iteration{0U};
  double average_strategy_weight{1.0};
  bool reset_average_strategy{true};
};

FiniteProductionSchedulePoint finite_production_schedule(const std::uint64_t iteration) noexcept {
  const std::uint64_t zero_based_iteration = iteration == 0U ? 0U : iteration - 1U;
  std::uint64_t epoch_start = 0U;
  if (zero_based_iteration != 0U) {
    const auto highest_bit = static_cast<unsigned>(std::numeric_limits<std::uint64_t>::digits - 1U -
                                                   std::countl_zero(zero_based_iteration));
    epoch_start = std::min<std::uint64_t>(std::uint64_t{1} << (highest_bit & ~1U), 64U);
  }
  const auto epoch_index = zero_based_iteration - epoch_start;
  const double sample_index = static_cast<double>(epoch_index) + 1.0;
  return {
      .regret_discount_iteration =
          zero_based_iteration <= 64U ? zero_based_iteration : zero_based_iteration - 1U,
      .average_strategy_weight = sample_index * sample_index * sample_index,
      .reset_average_strategy = epoch_index == 0U,
  };
}

void reset_average_strategy(SolverCheckpoint &checkpoint) {
  for (auto &[key, buffer] : checkpoint.information_sets) {
    static_cast<void>(key);
    std::ranges::fill(buffer.cumulative_strategy, 0.0);
  }
}

void apply_production_player_delta(SolverCheckpoint &checkpoint, const IterationDelta &delta,
                                   const FiniteProductionSchedulePoint &schedule,
                                   const std::uint8_t updating_player) {
  const double regret_iteration = static_cast<double>(schedule.regret_discount_iteration);
  const double powered = std::pow(regret_iteration, 1.5);
  const double positive_discount = powered / (powered + 1.0);
  constexpr double negative_discount = 0.5;
  for (auto &[key, buffer] : checkpoint.information_sets) {
    if (buffer.player != updating_player) {
      continue;
    }
    const auto regret_found = delta.regret.find(key);
    const auto strategy_found = delta.strategy.find(key);
    for (std::size_t index = 0U; index < buffer.actions.size(); ++index) {
      const double regret_delta =
          regret_found == delta.regret.end() ? 0.0 : regret_found->second[index];
      const double strategy_delta =
          strategy_found == delta.strategy.end() ? 0.0 : strategy_found->second[index];
      buffer.cumulative_regret[index] *=
          buffer.cumulative_regret[index] > 0.0 ? positive_discount : negative_discount;
      buffer.cumulative_regret[index] += regret_delta;
      buffer.cumulative_strategy[index] += schedule.average_strategy_weight * strategy_delta;
    }
  }
}

std::uint64_t double_bits(const double value) { return std::bit_cast<std::uint64_t>(value); }

double bits_double(const std::uint64_t value) { return std::bit_cast<double>(value); }

} // namespace

Result<SolveResult, SolverError> solve_finite_game(const FiniteGame &game,
                                                   const SolverConfig &config,
                                                   const SolverCheckpoint *resume_from) {
  const auto validation = validate_finite_game(game);
  if (!validation) {
    return Result<SolveResult, SolverError>::failure(validation.error());
  }
  if (!valid_configuration(config)) {
    return Result<SolveResult, SolverError>::failure(SolverError::InvalidConfiguration);
  }

  SolverCheckpoint checkpoint;
  if (resume_from != nullptr) {
    if (!compatible_resume(*resume_from, game, config) ||
        resume_from->completed_iterations > config.iterations) {
      return Result<SolveResult, SolverError>::failure(SolverError::GameMismatch);
    }
    checkpoint = *resume_from;
    checkpoint.config.iterations = config.iterations;
  } else {
    const auto buffers = make_information_set_buffers(game);
    if (!buffers) {
      return Result<SolveResult, SolverError>::failure(buffers.error());
    }
    checkpoint.game_fingerprint = validation.value().fingerprint;
    checkpoint.config = config;
    checkpoint.rng_state = config.seed;
    checkpoint.information_sets = buffers.value();
  }

  std::uint64_t traversed_nodes = 0;
  std::unique_ptr<ExactTraversalWorkers> parallel_workers;
  if (can_parallelize_root(game, config)) {
    parallel_workers = std::make_unique<ExactTraversalWorkers>(game, config.thread_count);
  }
  for (std::uint64_t iteration = checkpoint.completed_iterations + 1U;
       iteration <= config.iterations; ++iteration) {
    if (config.algorithm == SolverAlgorithm::ProductionDcfr) {
      const auto schedule = finite_production_schedule(iteration);
      if (schedule.reset_average_strategy) {
        reset_average_strategy(checkpoint);
      }
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        const StrategyProfile strategy = strategy_from_buffers(checkpoint.information_sets, false);
        IterationDelta delta;
        static_cast<void>(
            traverse_alternating(game, game.root, strategy, player, {1.0, 1.0}, 1.0, delta));
        apply_production_player_delta(checkpoint, delta, schedule, player);
        traversed_nodes += delta.traversed_nodes;
      }
      checkpoint.completed_iterations = iteration;
      continue;
    }
    const StrategyProfile strategy = strategy_from_buffers(checkpoint.information_sets, false);
    IterationDelta delta;
    if (config.algorithm == SolverAlgorithm::ExternalSamplingMccfr ||
        config.algorithm == SolverAlgorithm::LinearMccfr) {
      for (std::uint8_t player = 0; player < 2U; ++player) {
        static_cast<void>(traverse_external_sampling(game, game.root, strategy, player, {1.0, 1.0},
                                                     delta, checkpoint.rng_state));
      }
    } else {
      if (parallel_workers) {
        parallel_workers->traverse(strategy, delta);
      } else {
        traverse_full_iteration(game, strategy, delta);
      }
    }
    apply_iteration_delta(checkpoint, delta, iteration);
    checkpoint.completed_iterations = iteration;
    traversed_nodes += delta.traversed_nodes;
  }

  for (const auto &[key, buffer] : checkpoint.information_sets) {
    static_cast<void>(key);
    if (!finite_vector(buffer.cumulative_regret) || !finite_vector(buffer.cumulative_strategy)) {
      return Result<SolveResult, SolverError>::failure(SolverError::NumericalFailure);
    }
  }

  SolveResult result;
  result.checkpoint = checkpoint;
  result.average_strategy = strategy_from_buffers(checkpoint.information_sets, true);
  result.traversed_nodes = traversed_nodes;
  for (const auto &[key, strategy] : result.average_strategy) {
    static_cast<void>(key);
    double sum = 0.0;
    for (const double probability : strategy.probabilities) {
      sum += probability;
    }
    result.maximum_normalization_error =
        std::max(result.maximum_normalization_error, std::abs(sum - 1.0));
  }
  const auto profile_validation = validate_strategy_profile(game, result.average_strategy);
  if (!profile_validation) {
    return Result<SolveResult, SolverError>::failure(profile_validation.error());
  }
  return Result<SolveResult, SolverError>::success(std::move(result));
}

Result<StrategyProfile, SolverError> current_strategy_profile(const SolverCheckpoint &checkpoint) {
  if (checkpoint.information_sets.empty()) {
    return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  return Result<StrategyProfile, SolverError>::success(
      strategy_from_buffers(checkpoint.information_sets, false));
}

Result<StrategyProfile, SolverError> average_strategy_profile(const SolverCheckpoint &checkpoint) {
  if (checkpoint.information_sets.empty()) {
    return Result<StrategyProfile, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  return Result<StrategyProfile, SolverError>::success(
      strategy_from_buffers(checkpoint.information_sets, true));
}

Result<std::string, SolverError> serialize_solver_checkpoint(const SolverCheckpoint &checkpoint) {
  if (checkpoint.major != SolverCheckpoint::format_major ||
      checkpoint.minor > SolverCheckpoint::format_minor || checkpoint.information_sets.empty() ||
      checkpoint.game_fingerprint.empty()) {
    return Result<std::string, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  std::ostringstream output;
  output << "GTOSD_CFR_CHECKPOINT " << checkpoint.major << ' ' << checkpoint.minor << '\n'
         << std::quoted(checkpoint.game_fingerprint) << '\n'
         << static_cast<unsigned>(checkpoint.config.algorithm) << ' '
         << checkpoint.config.iterations << ' ' << checkpoint.config.seed << ' '
         << checkpoint.config.thread_count << ' ' << checkpoint.config.averaging_delay << ' '
         << double_bits(checkpoint.config.dcfr.positive_regret_exponent) << ' '
         << double_bits(checkpoint.config.dcfr.negative_regret_exponent) << ' '
         << double_bits(checkpoint.config.dcfr.strategy_exponent) << '\n'
         << checkpoint.completed_iterations << ' ' << checkpoint.rng_state << '\n'
         << checkpoint.information_sets.size() << '\n';
  for (const auto &[key, buffer] : checkpoint.information_sets) {
    output << std::quoted(key) << ' ' << static_cast<unsigned>(buffer.player) << ' '
           << buffer.actions.size() << '\n';
    for (std::size_t index = 0; index < buffer.actions.size(); ++index) {
      output << buffer.actions[index] << ' ' << double_bits(buffer.cumulative_regret[index]) << ' '
             << double_bits(buffer.cumulative_strategy[index]) << '\n';
    }
  }
  return Result<std::string, SolverError>::success(output.str());
}

Result<SolverCheckpoint, SolverError> deserialize_solver_checkpoint(const std::string &serialized) {
  std::istringstream input(serialized);
  std::string magic;
  SolverCheckpoint checkpoint;
  if (!(input >> magic >> checkpoint.major >> checkpoint.minor) ||
      magic != "GTOSD_CFR_CHECKPOINT") {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  if (checkpoint.major != SolverCheckpoint::format_major) {
    return Result<SolverCheckpoint, SolverError>::failure(
        SolverError::UnsupportedCheckpointVersion);
  }
  if (checkpoint.minor > SolverCheckpoint::format_minor) {
    return Result<SolverCheckpoint, SolverError>::failure(
        SolverError::UnsupportedCheckpointVersion);
  }
  unsigned algorithm = 0;
  std::uint64_t positive_bits = 0;
  std::uint64_t negative_bits = 0;
  std::uint64_t strategy_bits = 0;
  if (!(input >> std::quoted(checkpoint.game_fingerprint) >> algorithm >>
        checkpoint.config.iterations >> checkpoint.config.seed >> checkpoint.config.thread_count >>
        checkpoint.config.averaging_delay >> positive_bits >> negative_bits >> strategy_bits >>
        checkpoint.completed_iterations >> checkpoint.rng_state)) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  if (algorithm > static_cast<unsigned>(SolverAlgorithm::LinearMccfr)) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  checkpoint.config.algorithm = static_cast<SolverAlgorithm>(algorithm);
  checkpoint.config.dcfr.positive_regret_exponent = bits_double(positive_bits);
  checkpoint.config.dcfr.negative_regret_exponent = bits_double(negative_bits);
  checkpoint.config.dcfr.strategy_exponent = bits_double(strategy_bits);

  std::size_t information_set_count = 0;
  if (!(input >> information_set_count) || information_set_count == 0U ||
      information_set_count > 1'000'000U) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  for (std::size_t set_index = 0; set_index < information_set_count; ++set_index) {
    std::string key;
    unsigned player = 0;
    std::size_t action_count = 0;
    if (!(input >> std::quoted(key) >> player >> action_count) || key.empty() || player > 1U ||
        action_count == 0U || action_count > 1'024U) {
      return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
    }
    InformationSetBuffer buffer;
    buffer.player = static_cast<std::uint8_t>(player);
    for (std::size_t action_index = 0; action_index < action_count; ++action_index) {
      GameActionId action = 0;
      std::uint64_t regret_bits = 0;
      std::uint64_t cumulative_strategy_bits = 0;
      if (!(input >> action >> regret_bits >> cumulative_strategy_bits)) {
        return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
      }
      buffer.actions.push_back(action);
      buffer.cumulative_regret.push_back(bits_double(regret_bits));
      buffer.cumulative_strategy.push_back(bits_double(cumulative_strategy_bits));
    }
    if (!finite_vector(buffer.cumulative_regret) || !finite_vector(buffer.cumulative_strategy) ||
        !checkpoint.information_sets.emplace(std::move(key), std::move(buffer)).second) {
      return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
    }
  }
  input >> std::ws;
  if (!input.eof() || checkpoint.completed_iterations > checkpoint.config.iterations ||
      !valid_configuration(checkpoint.config)) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::InvalidCheckpoint);
  }
  return Result<SolverCheckpoint, SolverError>::success(std::move(checkpoint));
}

Result<bool, SolverError> save_solver_checkpoint(const SolverCheckpoint &checkpoint,
                                                 const std::string &path) {
  const auto serialized = serialize_solver_checkpoint(checkpoint);
  if (!serialized || path.empty()) {
    return Result<bool, SolverError>::failure(serialized ? SolverError::IoFailure
                                                         : serialized.error());
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output || !(output << serialized.value()) || !output.flush()) {
    return Result<bool, SolverError>::failure(SolverError::IoFailure);
  }
  return Result<bool, SolverError>::success(true);
}

Result<SolverCheckpoint, SolverError> load_solver_checkpoint(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::IoFailure);
  }
  std::ostringstream serialized;
  serialized << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return Result<SolverCheckpoint, SolverError>::failure(SolverError::IoFailure);
  }
  return deserialize_solver_checkpoint(serialized.str());
}

const char *solver_algorithm_name(const SolverAlgorithm algorithm) noexcept {
  switch (algorithm) {
  case SolverAlgorithm::VanillaCfr:
    return "vanilla_cfr";
  case SolverAlgorithm::CfrPlus:
    return "cfr_plus";
  case SolverAlgorithm::LinearCfr:
    return "linear_cfr";
  case SolverAlgorithm::Dcfr:
    return "dcfr";
  case SolverAlgorithm::ExternalSamplingMccfr:
    return "external_sampling_mccfr";
  case SolverAlgorithm::ProductionDcfr:
    return "production_dcfr";
  case SolverAlgorithm::LinearMccfr:
    return "linear_mccfr";
  }
  return "unknown";
}

} // namespace gtosd
