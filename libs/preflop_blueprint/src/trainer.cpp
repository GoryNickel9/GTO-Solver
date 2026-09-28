#include "gtosd/preflop_blueprint/trainer.hpp"
#include "gtosd/preflop_blueprint/abstract_best_response.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"

#include "binary_io.hpp"
#include "stream_io.hpp"

#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/best_response.hpp"

#include "hashing.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__SSE__)
#include <xmmintrin.h>
#endif
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace gtosd::preflop_blueprint {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint32_t no_unit = 0xFFFF'FFFFU;
constexpr double ante_scale = 1.0 / static_cast<double>(Money::units_per_ante);
constexpr std::array<char, 8> checkpoint_magic{'G', 'T', 'O', 'S', 'D', 'C', 'K', 'P'};
constexpr std::uint32_t checkpoint_version = 2U;
constexpr std::uint64_t profile_sample_stride = 1'024U;
constexpr std::size_t export_block_entries = 65'536U;

bool all_zero(const double *values) noexcept {
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    if (values[hand] != 0.0) {
      return false;
    }
  }
  return true;
}

bool disjoint(const std::array<std::uint8_t, 2> &left,
              const std::array<std::uint8_t, 2> &right) noexcept {
  return left[0] != right[0] && left[0] != right[1] && left[1] != right[0] &&
         left[1] != right[1];
}

bool supported_diagnostic_prior(const std::vector<card_abstraction::BoardHistory> &boards,
                                const std::array<std::vector<std::uint8_t>, 2> &allowed) {
  if (boards.empty()) {
    return false;
  }
  const auto &combos = card_abstraction::combo_table();
  for (const auto &board : boards) {
    const auto mask = board.flop[0].mask() | board.flop[1].mask() | board.flop[2].mask() |
                      board.turn.mask() | board.river.mask();
    for (std::size_t combo = 0; combo < combos.masks.size(); ++combo) {
      if ((allowed[0][combo] != 0U || allowed[1][combo] != 0U) &&
          (combos.masks[combo] & mask) != 0U) {
        return false;
      }
    }
  }
  for (std::uint8_t player = 0; player < 2U; ++player) {
    std::uint32_t degree = 0U;
    for (std::size_t combo = 0; combo < combos.masks.size(); ++combo) {
      if (allowed[player][combo] == 0U) {
        continue;
      }
      std::uint32_t compatible = 0U;
      for (std::size_t other = 0; other < combos.masks.size(); ++other) {
        if (allowed[1U - player][other] != 0U &&
            (combos.masks[combo] & combos.masks[other]) == 0U) {
          ++compatible;
        }
      }
      if (compatible == 0U || (degree != 0U && degree != compatible)) {
        return false;
      }
      degree = compatible;
    }
    if (degree == 0U) {
      return false;
    }
  }
  return true;
}

using binary_io::append_little;
using binary_io::append_little32;

// Every numeric increment is computed in double and rounded once when the
// cell is stored: a narrower storage introduces a storage error only.
template <typename Number> inline void store(Number &cell, const double value) noexcept {
  cell = static_cast<Number>(value);
}

// Software prefetch of a table row that will be read a few steps later: the rows of a
// batch are scattered over gigabytes, so each one is a cache and TLB miss otherwise.
// A prefetch never faults and changes no value: the arithmetic stays bit-identical.
inline void prefetch_read(const void *const address) noexcept {
#if defined(_MSC_VER) || defined(__SSE__)
  _mm_prefetch(static_cast<const char *>(address), _MM_HINT_T0);
#else
  (void)address;
#endif
}


// Regret matching of one row: the current strategy from the (possibly
// narrow) stored regrets, evaluated in double exactly as the dense snapshot did.
template <typename RegretT>
inline void regret_match_row(const RegretT *regrets, const std::uint8_t actions,
                             double *out) noexcept {
  double positive = 0.0;
  for (std::uint8_t action = 0; action < actions; ++action) {
    positive += std::max(0.0, static_cast<double>(regrets[action]));
  }
  if (positive <= 0.0) {
    const double uniform = 1.0 / actions;
    for (std::uint8_t action = 0; action < actions; ++action) {
      out[action] = uniform;
    }
  } else {
    for (std::uint8_t action = 0; action < actions; ++action) {
      out[action] = std::max(0.0, static_cast<double>(regrets[action])) / positive;
    }
  }
}

template <typename Function>
void dispatch_tables(const TableStorage storage, std::vector<double> &regrets_d,
                     std::vector<double> &sums_d, std::vector<float> &regrets_f,
                     std::vector<float> &sums_f, Function &&function) {
  switch (storage) {
  case TableStorage::Double:
    function(regrets_d.data(), sums_d.data());
    return;
  case TableStorage::MixedFloatSums:
    function(regrets_d.data(), sums_f.data());
    return;
  case TableStorage::Float32:
    function(regrets_f.data(), sums_f.data());
    return;
  }
}

template <typename Function>
void dispatch_tables(const TableStorage storage, const std::vector<double> &regrets_d,
                     const std::vector<double> &sums_d, const std::vector<float> &regrets_f,
                     const std::vector<float> &sums_f, Function &&function) {
  switch (storage) {
  case TableStorage::Double:
    function(regrets_d.data(), sums_d.data());
    return;
  case TableStorage::MixedFloatSums:
    function(regrets_d.data(), sums_f.data());
    return;
  case TableStorage::Float32:
    function(regrets_f.data(), sums_f.data());
    return;
  }
}

} // namespace

class ParallelExecutor {
public:
  explicit ParallelExecutor(const unsigned threads) {
    const auto worker_count = static_cast<std::size_t>(std::max(1U, threads));
    workers_.reserve(worker_count - 1U);
    for (unsigned thread = 1U; thread < worker_count; ++thread)
      workers_.emplace_back([this, thread] { worker_loop(thread); });
  }

  ~ParallelExecutor() {
    {
      const std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    start_.notify_all();
    for (auto &worker : workers_)
      worker.join();
  }

  ParallelExecutor(const ParallelExecutor &) = delete;
  ParallelExecutor &operator=(const ParallelExecutor &) = delete;

  template <typename Function>
  void run(const std::size_t count, Function &&function) {
    if (workers_.empty() || count <= 1U) {
      for (std::size_t index = 0; index < count; ++index)
        function(index, 0U);
      return;
    }
    {
      const std::lock_guard lock(mutex_);
      function_ = std::forward<Function>(function);
      count_ = count;
      next_.store(0U, std::memory_order_relaxed);
      remaining_ = workers_.size();
      ++generation_;
    }
    start_.notify_all();
    execute(0U);
    {
      std::unique_lock lock(mutex_);
      finished_.wait(lock, [this] { return remaining_ == 0U; });
      function_ = {};
    }
  }

private:
  void execute(const unsigned thread) {
    for (auto index = next_.fetch_add(1U, std::memory_order_relaxed); index < count_;
         index = next_.fetch_add(1U, std::memory_order_relaxed))
      function_(index, thread);
  }

  void worker_loop(const unsigned thread) {
    std::uint64_t observed_generation = 0U;
    for (;;) {
      {
        std::unique_lock lock(mutex_);
        start_.wait(lock, [this, observed_generation] {
          return stopping_ || generation_ != observed_generation;
        });
        if (stopping_)
          return;
        observed_generation = generation_;
      }
      execute(thread);
      {
        const std::lock_guard lock(mutex_);
        --remaining_;
        if (remaining_ == 0U)
          finished_.notify_one();
      }
    }
  }

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable start_;
  std::condition_variable finished_;
  std::function<void(std::size_t, unsigned)> function_;
  std::atomic<std::size_t> next_{0U};
  std::size_t count_{0U};
  std::size_t remaining_{0U};
  std::uint64_t generation_{0U};
  bool stopping_{false};
};

struct Trainer::Unit {
  std::uint32_t root{0U};
  std::vector<double> hero_reach;
  std::vector<double> opponent_reach;
  std::vector<double> values;
};

struct Trainer::Workspace {
  struct Level {
    std::vector<double> child_reach;
    std::vector<double> child_values;
    std::vector<double> scratch;
    std::array<std::uint64_t, live_hand_count> cell_offsets{};
    std::array<std::uint64_t, live_hand_count> policy_offsets{};
  };
  std::vector<Level> levels;
  std::vector<double> regret_weight;
  std::vector<double> strategy_weight;
  std::uint64_t nodes_visited{0U};
  std::uint64_t decision_nodes_visited{0U};
  std::uint64_t hero_decision_nodes{0U};
  std::uint64_t opponent_decision_nodes{0U};
  std::uint64_t chance_nodes_visited{0U};
  std::uint64_t fold_terminals_visited{0U};
  std::uint64_t preflop_all_in_terminals_visited{0U};
  std::uint64_t postflop_showdown_terminals_visited{0U};
  std::uint64_t zero_reach_prunes{0U};
  std::uint64_t policy_rows_read{0U};
  std::uint64_t regret_cells_written{0U};
  std::uint64_t strategy_cells_written{0U};
  double sampled_hero_reach_seconds{0.0};
  double sampled_hero_update_seconds{0.0};
  double sampled_opponent_reach_seconds{0.0};
  double sampled_opponent_accumulate_seconds{0.0};
  double sampled_fold_terminal_seconds{0.0};
  double sampled_preflop_all_in_seconds{0.0};
  double sampled_postflop_showdown_seconds{0.0};

  explicit Workspace(const std::size_t depth) {
    levels.resize(depth);
    for (auto &level : levels) {
      level.child_reach.assign(maximum_actions * live_hand_count, 0.0);
      level.child_values.assign(maximum_actions * live_hand_count, 0.0);
      level.scratch.assign(3U * live_hand_count, 0.0);
    }
    regret_weight.assign(live_hand_count, 0.0);
    strategy_weight.assign(live_hand_count, 0.0);
  }

  void reset_counters() noexcept {
    nodes_visited = decision_nodes_visited = hero_decision_nodes = opponent_decision_nodes =
        chance_nodes_visited = fold_terminals_visited = preflop_all_in_terminals_visited =
            postflop_showdown_terminals_visited = zero_reach_prunes = policy_rows_read =
                regret_cells_written = strategy_cells_written = 0U;
    sampled_hero_reach_seconds = sampled_hero_update_seconds = sampled_opponent_reach_seconds =
        sampled_opponent_accumulate_seconds = sampled_fold_terminal_seconds =
            sampled_preflop_all_in_seconds = sampled_postflop_showdown_seconds = 0.0;
  }

  [[nodiscard]] std::uint64_t bytes() const noexcept {
    std::uint64_t total = sizeof(Workspace) + levels.capacity() * sizeof(Level);
    for (const auto &level : levels)
      total += (level.child_reach.capacity() + level.child_values.capacity() +
                level.scratch.capacity()) *
               sizeof(double);
    return total + (regret_weight.capacity() + strategy_weight.capacity()) * sizeof(double);
  }
};

struct Trainer::BoardWork {
  BoardContext context;
  double weight{1.0};
  std::array<std::vector<double>, 2> initial_reach;
  std::array<std::vector<double>, 2> hand_probability;
  std::array<std::vector<double>, 2> pair_probability;
  // Slot of every hand in the compact policy of the current pass, per
  // postflop street (dense row in fixed-policy mode).
  std::array<std::array<std::uint32_t, live_hand_count>, 3> slot{};
  double context_cpu_seconds{0.0};
  double all_in_cpu_seconds{0.0};
  double reach_cpu_seconds{0.0};

  [[nodiscard]] std::uint64_t bytes() const noexcept {
    std::uint64_t total = sizeof(BoardWork);
    for (const auto *arrays : {&initial_reach, &hand_probability, &pair_probability})
      for (const auto &values : *arrays)
        total += values.capacity() * sizeof(double);
    return total;
  }
};

SubtreePartition SubtreePartition::build(const CompiledGame &game, const std::uint32_t target_nodes) {
  SubtreePartition partition;
  const auto &nodes = game.nodes();
  partition.is_top.assign(nodes.size(), 0U);
  const auto target = std::max<std::uint32_t>(1U, target_nodes);
  std::vector<std::uint32_t> stack{game.root()};
  while (!stack.empty()) {
    const auto id = stack.back();
    stack.pop_back();
    const auto &node = nodes[id];
    const auto size = node.subtree_end - node.id;
    if (size > target && node.action_count > 0U) {
      partition.is_top[id] = 1U;
      ++partition.top_nodes;
      for (const auto &edge : game.edges_of(id)) {
        stack.push_back(edge.child);
      }
      continue;
    }
    partition.unit_roots.push_back(id);
    partition.largest_unit_nodes = std::max(partition.largest_unit_nodes, size);
  }
  std::stable_sort(partition.unit_roots.begin(), partition.unit_roots.end(),
                   [&](const std::uint32_t left, const std::uint32_t right) {
                     const auto left_size = nodes[left].subtree_end - left;
                     const auto right_size = nodes[right].subtree_end - right;
                     return left_size > right_size;
                   });
  return partition;
}

Trainer::Trainer(const CompiledGame &game, const TrainerResources &resources,
                 const TrainerConfig &config)
    : game_(&game), resources_(resources), config_(config),
      executor_(std::make_unique<ParallelExecutor>(config.threads)),
      training_random_(config.training_seed), evaluation_random_(config.evaluation_seed) {}

Trainer::~Trainer() = default;

Result<std::unique_ptr<Trainer>, TrainerError>
Trainer::create(const CompiledGame &game, const TrainerResources &resources,
                const TrainerConfig &config, const TrainingBoards *boards,
                const HandSubsets *subsets) {
  using Outcome = Result<std::unique_ptr<Trainer>, TrainerError>;
  if (config.batch_boards == 0U || config.threads == 0U || config.flop_capacity == 0U ||
      config.turn_capacity == 0U || config.river_capacity == 0U || config.dcfr_alpha <= 0.0 ||
      config.dcfr_beta < 0.0 || config.dcfr_gamma <= 0.0 || game.config().player_count != 2U) {
    return Outcome::failure(TrainerError::InvalidConfiguration);
  }
  if (config.update_mode == UpdateMode::Alternating &&
      config.batch_boards > std::numeric_limits<std::uint32_t>::max() / 2U) {
    return Outcome::failure(TrainerError::InvalidConfiguration);
  }
  // The per-batch policy is rebuilt before every pass: the former experimental
  // reuse of a discount-invariant snapshot no longer has a meaning.
  if (config.reuse_discount_invariant_policy) {
    return Outcome::failure(TrainerError::InvalidConfiguration);
  }
  // The 16-bit lazy-discount timestamps need an epoch of at most 65,535 iterations.
  if (config.lazy_discount &&
      (config.lazy_discount_epoch == 0U || config.lazy_discount_epoch > 65'535U)) {
    return Outcome::failure(TrainerError::InvalidConfiguration);
  }
  if (resources.ranks == nullptr) {
    return Outcome::failure(TrainerError::MissingResource);
  }
  std::unique_ptr<Trainer> trainer(new Trainer(game, resources, config));
  const auto initialized = trainer->initialize(boards, subsets);
  if (!initialized) {
    return Outcome::failure(initialized.error());
  }
  return Outcome::success(std::move(trainer));
}

Result<bool, TrainerError> Trainer::initialize(const TrainingBoards *boards,
                                               const HandSubsets *subsets,
                                               std::vector<double> *fixed_policy) {
  using Outcome = Result<bool, TrainerError>;
  const auto &stats = game_->stats();
  const bool postflop_decisions = stats.decision_nodes > stats.preflop_decisions;
  if (postflop_decisions) {
    if (resources_.catalog == nullptr || resources_.flop == nullptr || resources_.turn == nullptr ||
        resources_.river == nullptr) {
      return Outcome::failure(TrainerError::MissingResource);
    }
    const auto capacity = [&](const card_abstraction::BucketTable &table) {
      if (resources_.board_class_rows)
        return resources_.board_class_rows->count(table.street());
      if (resources_.history_rows)
        return resources_.history_rows->count(table.street());
      return resources_.class_rows ? resources_.class_rows->count(table.street())
                                   : static_cast<std::uint32_t>(table.capacity());
    };
    if (resources_.history_rows &&
        (resources_.class_rows || !resources_.history_rows->matches(*resources_.flop) ||
         !resources_.history_rows->matches(*resources_.turn) ||
         !resources_.history_rows->matches(*resources_.river)))
      return Outcome::failure(TrainerError::InvalidConfiguration);
    if (resources_.class_rows && (!resources_.class_rows->matches(*resources_.flop) ||
                                  !resources_.class_rows->matches(*resources_.turn) ||
                                  !resources_.class_rows->matches(*resources_.river))) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
    if (resources_.board_class_rows &&
        (resources_.class_rows || resources_.history_rows ||
         !resources_.board_class_rows->matches(*resources_.flop) ||
         !resources_.board_class_rows->matches(*resources_.turn) ||
         !resources_.board_class_rows->matches(*resources_.river))) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
    if (capacity(*resources_.flop) != config_.flop_capacity ||
        capacity(*resources_.turn) != config_.turn_capacity ||
        capacity(*resources_.river) != config_.river_capacity) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
  }
  if (stats.preflop_all_in_runouts > 0U && resources_.all_in == nullptr) {
    return Outcome::failure(TrainerError::MissingResource);
  }
  all_in_available_ = false;
  if (stats.preflop_all_in_runouts > 0U) {
    constexpr std::size_t count = card_abstraction::combo_count;
    all_in_win_probability_.assign(count * count, 0.0);
    all_in_tie_probability_.assign(count * count, 0.0);
    for (std::uint16_t hand = 0; hand < count; ++hand) {
      for (std::uint16_t other = static_cast<std::uint16_t>(hand + 1U); other < count; ++other) {
        const auto outcome = resources_.all_in->outcome(hand, other);
        const auto total = outcome.total();
        if (total == 0U)
          continue;
        const auto forward = static_cast<std::size_t>(hand) * count + other;
        const auto reverse = static_cast<std::size_t>(other) * count + hand;
        all_in_win_probability_[forward] = static_cast<double>(outcome.wins) / total;
        all_in_win_probability_[reverse] = static_cast<double>(outcome.losses) / total;
        const auto tie = static_cast<double>(outcome.ties) / total;
        all_in_tie_probability_[forward] = tie;
        all_in_tie_probability_[reverse] = tie;
      }
    }
    all_in_available_ = true;
  }
  layout_ = layout_state(*game_, config_.flop_capacity, config_.turn_capacity,
                         config_.river_capacity);
  if (config_.lazy_discount &&
      (config_.scheme != WeightingScheme::Dcfr || config_.dcfr_beta != 0.0 ||
       fixed_policy != nullptr))
    return Outcome::failure(TrainerError::InvalidConfiguration);
  const auto node_count = static_cast<std::uint32_t>(game_->nodes().size());
  const auto target = config_.partition_target_nodes != 0U
                          ? config_.partition_target_nodes
                          : std::max<std::uint32_t>(256U, node_count / 128U);
  partition_ = SubtreePartition::build(*game_, target);
  unit_of_node_.assign(node_count, no_unit);
  units_.resize(partition_.unit_roots.size());
  for (std::size_t index = 0; index < units_.size(); ++index) {
    units_[index].root = partition_.unit_roots[index];
    units_[index].hero_reach.assign(live_hand_count, 0.0);
    units_[index].opponent_reach.assign(live_hand_count, 0.0);
    units_[index].values.assign(live_hand_count, 0.0);
    unit_of_node_[units_[index].root] = static_cast<std::uint32_t>(index);
  }
  fixed_policy_evaluation_ = fixed_policy != nullptr;
  const bool float_regrets = config_.storage == TableStorage::Float32;
  const bool float_sums = config_.storage != TableStorage::Double;
  if (float_regrets)
    regrets_f32_.assign(layout_.entries, 0.0f);
  else
    regrets_.assign(layout_.entries, 0.0);
  discount_offsets_.assign(game_->nodes().size(), no_offset);
  if (config_.lazy_discount) {
    std::uint64_t discount_rows = 0U;
    for (const auto &node : game_->nodes()) {
      if (node.kind != NodeKind::Decision)
        continue;
      discount_offsets_[node.id] = discount_rows;
      discount_rows += StateLayout::rows_for(node.street, config_.flop_capacity,
                                             config_.turn_capacity, config_.river_capacity);
    }
    if (discount_rows > std::numeric_limits<std::size_t>::max())
      return Outcome::failure(TrainerError::InvalidConfiguration);
    discount_iterations_.assign(static_cast<std::size_t>(discount_rows), std::uint16_t{0U});
    discount_epoch_base_ = 0U;
  }
  compact_offsets_.assign(game_->nodes().size(), no_offset);
  if (fixed_policy_evaluation_) {
    if (fixed_policy->size() != layout_.entries)
      return Outcome::failure(TrainerError::InvalidConfiguration);
    compact_policy_ = std::move(*fixed_policy);
    compact_offsets_ = layout_.offsets;
  } else {
    if (float_sums)
      strategy_sums_f32_.assign(layout_.entries, 0.0f);
    else
      strategy_sums_.assign(layout_.entries, 0.0);
    compact_policy_.clear();
  }
  const auto depth = static_cast<std::size_t>(stats.maximum_depth) + 2U;
  workspaces_.clear();
  for (unsigned thread = 0; thread < config_.threads; ++thread) {
    workspaces_.push_back(std::make_unique<Workspace>(depth));
  }

  for (auto &mask : hand_masks_) {
    mask.assign(630U, 1U);
  }
  subsets_ = false;
  if (subsets != nullptr) {
    for (std::uint8_t player = 0; player < 2U; ++player) {
      if (subsets->combos[player].empty()) {
        continue;
      }
      subsets_ = true;
      hand_masks_[player].assign(630U, 0U);
      for (const auto combo : subsets->combos[player]) {
        if (combo >= 630U) {
          return Outcome::failure(TrainerError::InvalidConfiguration);
        }
        hand_masks_[player][combo] = 1U;
      }
    }
  }

  board_list_.clear();
  board_weights_.clear();
  board_cumulative_.clear();
  sample_boards_ = true;
  if (boards != nullptr && !boards->histories.empty()) {
    if (boards->weights.size() != boards->histories.size()) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
    double total = 0.0;
    for (const auto weight : boards->weights) {
      if (!(weight > 0.0) || !std::isfinite(weight)) {
        return Outcome::failure(TrainerError::InvalidConfiguration);
      }
      total += weight;
    }
    board_list_ = boards->histories;
    for (const auto weight : boards->weights) {
      board_weights_.push_back(weight / total);
      board_cumulative_.push_back(board_weights_.back() +
                                  (board_cumulative_.empty() ? 0.0 : board_cumulative_.back()));
    }
    board_cumulative_.back() = 1.0;
    sample_boards_ = boards->sample;
  } else if (resources_.catalog == nullptr) {
    return Outcome::failure(TrainerError::MissingResource);
  }

  const auto &config = game_->config();
  initial_pot_antes_ =
      static_cast<double>(config.ante.units() * config.player_count + config.button_blind.units()) *
      ante_scale;
  stack_antes_ = static_cast<double>(config.effective_stack.units()) * ante_scale;

  std::string identity = "gtosd.preflop_blueprint_trainer.v2|" + game_->fingerprint() + "|" +
                         game_config_fingerprint(config) + "|";
  identity += resources_.flop ? resources_.flop->fingerprint() : std::string("no_flop");
  identity += "|";
  identity += resources_.turn ? resources_.turn->fingerprint() : std::string("no_turn");
  identity += "|";
  identity += resources_.river ? resources_.river->fingerprint() : std::string("no_river");
  identity += "|";
  identity += resources_.catalog ? resources_.catalog->fingerprint() : std::string("no_catalog");
  identity += "|" + std::to_string(config_.flop_capacity) + "/" +
              std::to_string(config_.turn_capacity) + "/" +
              std::to_string(config_.river_capacity) + "|" +
              std::to_string(config_.batch_boards) + "|" + weighting_scheme_name(config_.scheme) +
              "|" + update_mode_name(config_.update_mode) + "|" +
              std::to_string(config_.dcfr_alpha) + "/" + std::to_string(config_.dcfr_beta) + "/" +
              std::to_string(config_.dcfr_gamma) + "|" + std::to_string(config_.training_seed) +
              "|" + std::to_string(config_.evaluation_seed) + "|" +
              (config_.lazy_discount ? "lazy-discount-v2-hybrid|" : "eager-discount|") +
              (sample_boards_ ? "sampled" : "exact") + "|" +
              std::to_string(board_list_.size()) + "|" + (subsets_ ? "subsets" : "all_hands");
  if (!board_list_.empty() || subsets_) {
    // The number of boards and presence of subsets do not identify a game.
    // Include their contents so a resume cannot silently change the chance
    // law or the private-card support while keeping the accumulated regrets.
    std::string diagnostic_input;
    for (std::size_t index = 0; index < board_list_.size(); ++index) {
      const auto &board = board_list_[index];
      for (const auto card : board.flop) {
        diagnostic_input.push_back(static_cast<char>(card.value()));
      }
      diagnostic_input.push_back(static_cast<char>(board.turn.value()));
      diagnostic_input.push_back(static_cast<char>(board.river.value()));
      append_little(diagnostic_input, std::bit_cast<std::uint64_t>(board_weights_[index]));
    }
    for (const auto &mask : hand_masks_) {
      diagnostic_input.append(reinterpret_cast<const char *>(mask.data()), mask.size());
    }
    identity += "|diagnostic-input:" + detail::hex64_text(detail::fnv1a_text(diagnostic_input));
  }
  if (resources_.class_rows) {
    identity += "|class-major-rows-v1";
  }
  if (resources_.history_rows)
    identity += "|history-rows=" + resources_.history_rows->fingerprint();
  if (resources_.board_class_rows)
    identity += "|" + resources_.board_class_rows->fingerprint();
  // Double storage keeps the historical identity; a narrower storage cannot
  // silently resume a double checkpoint.
  if (config_.storage != TableStorage::Double)
    identity += std::string("|storage=") + table_storage_name(config_.storage);
  identity_ = "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(identity));
  return Outcome::success(true);
}

Result<bool, TrainerError> Trainer::prepare_board(const card_abstraction::BoardHistory &history,
                                                  const double weight, BoardWork &work) const {
  using Outcome = Result<bool, TrainerError>;
  const auto prepare_started = config_.detailed_profile ? Clock::now() : Clock::time_point{};
  work.context_cpu_seconds = 0.0;
  work.all_in_cpu_seconds = 0.0;
  work.reach_cpu_seconds = 0.0;
  AbstractionTables tables;
  tables.catalog = resources_.catalog;
  tables.flop = resources_.flop;
  tables.turn = resources_.turn;
  tables.river = resources_.river;
  tables.class_rows = resources_.class_rows;
  tables.history_rows = resources_.history_rows;
  tables.board_class_rows = resources_.board_class_rows;
  const bool with_tables = resources_.catalog != nullptr && resources_.flop != nullptr &&
                           resources_.turn != nullptr && resources_.river != nullptr;
  auto context = BoardContext::build(history, *resources_.ranks, with_tables ? &tables : nullptr);
  if (!context) {
    return Outcome::failure(TrainerError::BoardFailure);
  }
  work.context = std::move(context.value());
  auto phase = config_.detailed_profile ? Clock::now() : Clock::time_point{};
  if (config_.detailed_profile)
    work.context_cpu_seconds =
        std::chrono::duration<double>(phase - prepare_started).count();
  work.weight = weight;
  if (fixed_policy_evaluation_) {
    // The fixed table keeps the dense layout: the slot of a hand is its row.
    for (std::size_t street = 0; street < 3U; ++street) {
      const auto current = static_cast<Street>(street + 1U);
      const bool present = work.context.has_buckets(current);
      for (std::uint16_t hand = 0; hand < live_hand_count; ++hand)
        work.slot[street][hand] = present ? work.context.row(current, hand) : 0U;
    }
  }
  const auto combos = work.context.combo_ids();
  const auto cards = work.context.cards();
  for (std::uint8_t player = 0; player < 2U; ++player) {
    work.initial_reach[player].assign(live_hand_count, 0.0);
    work.hand_probability[player].assign(live_hand_count, 0.0);
    work.pair_probability[player].assign(live_hand_count, 0.0);
    std::uint32_t live = 0U;
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      if (hand_masks_[player][combos[hand]] != 0U) {
        work.initial_reach[player][hand] = 1.0;
        ++live;
      }
    }
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      if (work.initial_reach[player][hand] == 0.0) {
        continue;
      }
      work.hand_probability[player][hand] = live > 0U ? 1.0 / live : 0.0;
    }
  }
  for (std::uint8_t player = 0; player < 2U; ++player) {
    const auto opponent = static_cast<std::uint8_t>(1U - player);
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      if (work.initial_reach[player][hand] == 0.0) {
        continue;
      }
      std::uint32_t compatible = 0U;
      if (!subsets_) {
        compatible = static_cast<std::uint32_t>(live_hand_count - 1U - 2U * (hands_per_card - 1U));
      } else {
        for (std::size_t other = 0; other < live_hand_count; ++other) {
          if (other != hand && work.initial_reach[opponent][other] != 0.0 &&
              disjoint(cards[hand], cards[other])) {
            ++compatible;
          }
        }
      }
      work.pair_probability[player][hand] = compatible > 0U ? 1.0 / compatible : 0.0;
    }
  }
  if (config_.detailed_profile)
    work.reach_cpu_seconds = std::chrono::duration<double>(Clock::now() - phase).count();
  return Outcome::success(true);
}

Trainer::ActiveRows Trainer::collect_active_rows(const std::vector<BoardWork> &batch) const {
  ActiveRows active;
  active[0].resize(81U);
  std::iota(active[0].begin(), active[0].end(), 0U);
  for (std::size_t street = 1; street < 4; ++street) {
    const auto row_count = StateLayout::rows_for(static_cast<Street>(street),
                                                 config_.flop_capacity,
                                                 config_.turn_capacity,
                                                 config_.river_capacity);
    std::vector<std::uint64_t> bits((row_count + 63U) / 64U, 0U);
    for (const auto &board : batch) {
      if (!board.context.has_buckets(static_cast<Street>(street)))
        continue;
      for (std::uint16_t hand = 0; hand < live_hand_count; ++hand) {
        const auto row = board.context.row(static_cast<Street>(street), hand);
        bits[row / 64U] |= std::uint64_t{1U} << (row % 64U);
      }
    }
    auto &rows = active[street];
    rows.reserve(std::min<std::size_t>(row_count, batch.size() * live_hand_count));
    for (std::size_t word_index = 0; word_index < bits.size(); ++word_index) {
      auto word = bits[word_index];
      while (word != 0U) {
        const auto bit = static_cast<std::size_t>(std::countr_zero(word));
        const auto row = word_index * 64U + bit;
        if (row < row_count)
          rows.push_back(static_cast<std::uint32_t>(row));
        word &= word - 1U;
      }
    }
  }
  return active;
}

void Trainer::assign_slots(std::vector<BoardWork> &batch, const ActiveRows &active) const {
  executor_->run(batch.size(), [&](const std::size_t index, const unsigned) {
    auto &work = batch[index];
    for (std::size_t street = 1; street < 4; ++street) {
      const auto current = static_cast<Street>(street);
      auto &slots = work.slot[street - 1U];
      if (!work.context.has_buckets(current)) {
        slots.fill(0U);
        continue;
      }
      const auto &rows = active[street];
      for (std::uint16_t hand = 0; hand < live_hand_count; ++hand) {
        const auto row = work.context.row(current, hand);
        const auto found = std::lower_bound(rows.begin(), rows.end(), row);
        slots[hand] = static_cast<std::uint32_t>(found - rows.begin());
      }
    }
  });
}

void Trainer::refresh_policy(std::vector<BoardWork> &batch, IterationTelemetry *telemetry) {
  const auto collect_started = Clock::now();
  const auto active = collect_active_rows(batch);
  assign_slots(batch, active);
  std::uint64_t total = 0U;
  for (const auto &node : game_->nodes()) {
    if (node.kind != NodeKind::Decision) {
      compact_offsets_[node.id] = no_offset;
      continue;
    }
    compact_offsets_[node.id] = total;
    total += static_cast<std::uint64_t>(active[static_cast<std::size_t>(node.street)].size()) *
             node.action_count;
  }
  compact_policy_.resize(static_cast<std::size_t>(total));
  const auto materialize_started = Clock::now();
  dispatch_tables(config_.storage, regrets_, strategy_sums_, regrets_f32_, strategy_sums_f32_,
                  [&](const auto *regrets, const auto *sums) {
                    executor_->run(game_->nodes().size(), [&](const std::size_t node_index,
                                                              const unsigned) {
                      const auto &node = game_->nodes()[node_index];
                      if (node.kind != NodeKind::Decision)
                        return;
                      const auto &selected = active[static_cast<std::size_t>(node.street)];
                      const auto actions = node.action_count;
                      const auto base = layout_.offsets[node.id];
                      auto *out = compact_policy_.data() + compact_offsets_[node.id];
                      const std::uint16_t *timestamps =
                          config_.lazy_discount
                              ? discount_iterations_.data() + discount_offsets_[node.id]
                              : nullptr;
                      const std::size_t refresh_distance = config_.prefetch_refresh_rows;
                      for (std::size_t index = 0; index < selected.size(); ++index) {
                        if (refresh_distance != 0U &&
                            index + refresh_distance < selected.size()) {
                          const auto ahead =
                              static_cast<std::uint64_t>(selected[index + refresh_distance]);
                          prefetch_read(regrets + base + ahead * actions);
                          prefetch_read(sums + base + ahead * actions);
                          if (timestamps != nullptr)
                            prefetch_read(timestamps + ahead);
                        }
                        const auto row = selected[index];
                        materialize_row(node.id, row, discount_target_);
                        regret_match_row(regrets + base + static_cast<std::uint64_t>(row) * actions,
                                         actions, out + index * actions);
                      }
                    });
                  });
  if (telemetry != nullptr) {
    telemetry->policy_refresh_collect_seconds +=
        std::chrono::duration<double>(materialize_started - collect_started).count();
    telemetry->policy_refresh_materialize_seconds +=
        std::chrono::duration<double>(Clock::now() - materialize_started).count();
    std::array<std::uint64_t, 4> cells{};
    for (const auto &node : game_->nodes()) {
      if (node.kind == NodeKind::Decision)
        cells[static_cast<std::size_t>(node.street)] +=
            static_cast<std::uint64_t>(active[static_cast<std::size_t>(node.street)].size()) *
            node.action_count;
    }
    for (std::size_t street = 0; street < 4U; ++street) {
      telemetry->policy_rows_materialized[street] += active[street].size();
      telemetry->policy_cells_materialized[street] += cells[street];
      std::uint64_t lookups = 0U;
      for (const auto &board : batch)
        if (street == 0U || board.context.has_buckets(static_cast<Street>(street)))
          lookups += live_hand_count;
      telemetry->policy_hand_lookups[street] += lookups;
    }
    telemetry->compact_policy_bytes = std::max<std::uint64_t>(telemetry->compact_policy_bytes,
                                                              total * sizeof(double));
  }
}

void Trainer::materialize_row(const std::uint32_t node_id, const std::uint32_t row,
                              const std::uint64_t iteration) {
  if (!config_.lazy_discount)
    return;
  // iterate() rebases the epoch before any worker reaches this point, so the slot
  // written below always fits 16 bits and no exception can escape a thread here.
  auto &slot = discount_iterations_[static_cast<std::size_t>(discount_offsets_[node_id] + row)];
  const std::uint64_t last = slot == 0U ? 0U : discount_epoch_base_ + (slot - 1U);
  if (last >= iteration) {
    // A row first seen while the target is still 0 is marked as touched (coverage);
    // the mark changes no value because the discount over (0, 0] is empty.
    if (slot == 0U)
      slot = static_cast<std::uint16_t>(iteration - discount_epoch_base_ + 1U);
    return;
  }
  apply_row_discount(node_id, row, last, iteration);
  slot = static_cast<std::uint16_t>(iteration - discount_epoch_base_ + 1U);
}

void Trainer::apply_row_discount(const std::uint32_t node_id, const std::uint32_t row,
                                 const std::uint64_t last, const std::uint64_t iteration) {
  const auto &node = game_->nodes()[node_id];
  const auto offset = layout_.offsets[node_id] +
                      static_cast<std::uint64_t>(row) * node.action_count;
  dispatch_tables(config_.storage, regrets_, strategy_sums_, regrets_f32_, strategy_sums_f32_,
                  [&](auto *regrets, auto *sums) {
                    bool nonzero = false;
                    for (std::uint8_t action = 0; action < node.action_count; ++action) {
                      nonzero = nonzero || regrets[offset + action] != 0 ||
                                sums[offset + action] != 0;
                    }
                    if (!nonzero)
                      return;
                    const auto first = static_cast<std::size_t>(last);
                    const auto final = static_cast<std::size_t>(iteration);
                    const auto skipped = iteration - last;
                    const double strategy =
                        strategy_discount_prefix_[final] / strategy_discount_prefix_[first];
                    for (std::uint8_t action = 0; action < node.action_count; ++action) {
                      auto &regret_cell = regrets[offset + action];
                      double regret = static_cast<double>(regret_cell);
                      if (regret > 0.0) {
                        // Product of the positive factors over (last, iteration] as one
                        // ratio of prefix products instead of one multiply per skipped
                        // iteration (rows are revisited after thousands of iterations).
                        regret *= positive_discount_prefix_[final] / positive_discount_prefix_[first];
                      } else if (skipped >
                                 static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                        regret = 0.0;
                      } else {
                        regret = std::ldexp(regret, -static_cast<int>(skipped));
                      }
                      store(regret_cell, regret);
                      auto &sum_cell = sums[offset + action];
                      store(sum_cell, static_cast<double>(sum_cell) * strategy);
                    }
                  });
}

void Trainer::prepare_discount_factors(const std::uint64_t iteration) {
  while (positive_discount_prefix_.size() <= iteration) {
    const double t = static_cast<double>(positive_discount_prefix_.size());
    const double power = std::pow(t, config_.dcfr_alpha);
    const double positive = power / (power + 1.0);
    const double strategy = std::pow(t / (t + 1.0), config_.dcfr_gamma);
    positive_discount_prefix_.push_back(positive_discount_prefix_.back() * positive);
    strategy_discount_prefix_.push_back(strategy_discount_prefix_.back() * strategy);
  }
}

void Trainer::materialize_discounts() { materialize_all_discounts(); }

double Trainer::positive_discount_ratio(const std::uint64_t last, const std::uint64_t iteration) {
  prepare_discount_factors(iteration);
  return positive_discount_prefix_[static_cast<std::size_t>(iteration)] /
         positive_discount_prefix_[static_cast<std::size_t>(last)];
}

void Trainer::materialize_all_discounts() {
  if (!config_.lazy_discount || discounts_materialized_)
    return;
  executor_->run(game_->nodes().size(), [&](const std::size_t node_index, const unsigned) {
    const auto &node = game_->nodes()[node_index];
    if (node.kind != NodeKind::Decision)
      return;
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    const auto *slots = discount_iterations_.data() + discount_offsets_[node.id];
    for (std::uint32_t row = 0; row < rows; ++row) {
      // Slot 0: never materialized, every cell still zero. Skipping it changes no value
      // and keeps the row coverage meaningful across mid-run saves and evaluations.
      if (slots[row] != 0U)
        materialize_row(node.id, row, discount_target_);
    }
  });
  discounts_materialized_ = true;
}

void Trainer::rebase_discount_epoch(const std::uint64_t target) {
  // Every touched row is materialized to `target` (one ratio of prefix products from
  // its last materialization) and re-timestamped to slot 1 of the new epoch; untouched
  // rows (slot 0, all cells zero) keep their sentinel, so coverage survives a rebase.
  const auto base = discount_epoch_base_;
  executor_->run(game_->nodes().size(), [&](const std::size_t node_index, const unsigned) {
    const auto &node = game_->nodes()[node_index];
    if (node.kind != NodeKind::Decision)
      return;
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    auto *slots = discount_iterations_.data() + discount_offsets_[node.id];
    for (std::uint32_t row = 0; row < rows; ++row) {
      const auto slot = slots[row];
      if (slot == 0U)
        continue;
      const std::uint64_t last = base + (slot - 1U);
      if (last < target)
        apply_row_discount(node.id, row, last, target);
      slots[row] = 1U;
    }
  });
  discount_epoch_base_ = target;
}

std::uint64_t Trainer::discount_last_iteration(const std::uint32_t node,
                                               const std::uint32_t row) const noexcept {
  if (!config_.lazy_discount || node >= discount_offsets_.size() ||
      discount_offsets_[node] == no_offset ||
      row >= StateLayout::rows_for(game_->nodes()[node].street, config_.flop_capacity,
                                   config_.turn_capacity, config_.river_capacity))
    return 0U;
  const auto slot = discount_iterations_[static_cast<std::size_t>(discount_offsets_[node] + row)];
  return slot == 0U ? 0U : discount_epoch_base_ + (slot - 1U);
}

Trainer::RowCoverage Trainer::row_coverage() const {
  RowCoverage coverage;
  if (!config_.lazy_discount)
    return coverage;
  // 4 KiB pages of the regret table counted from its first byte (the model of a page-aligned
  // sparse allocation) in one scan over the nodes, whose regions follow each other in id
  // order: a page shared by two nodes belongs to the street of the first node that reaches
  // it, in the totals and in the touched pages alike.
  constexpr std::uint64_t page_bytes = 4096U;
  const std::uint64_t cell_bytes =
      config_.storage == TableStorage::Float32 ? sizeof(float) : sizeof(double);
  struct PageCounter {
    bool any{false};
    std::uint64_t marked{0U};
    std::uint64_t add(const std::uint64_t first, const std::uint64_t last) noexcept {
      std::uint64_t added = 0U;
      if (!any || first > marked)
        added = last - first + 1U;
      else if (last > marked)
        added = last - marked;
      any = true;
      marked = std::max(marked, last);
      return added;
    }
  };
  PageCounter total_pages;
  PageCounter touched_pages;
  std::size_t previous_street = 0U;
  for (const auto &node : game_->nodes()) {
    if (node.kind != NodeKind::Decision)
      continue;
    const auto street = static_cast<std::size_t>(node.street);
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(node.action_count) * cell_bytes;
    const std::uint64_t first_byte = layout_.offsets[node.id] * cell_bytes;
    const auto *slots = discount_iterations_.data() + discount_offsets_[node.id];
    coverage.rows_total[street] += rows;
    if (rows == 0U || row_bytes == 0U)
      continue;
    const std::uint64_t node_first = first_byte / page_bytes;
    // The first page of this node may start inside an earlier node: it then belongs to
    // the street that owns the last counted page, also when a row of this node is the
    // first to touch it.
    const std::size_t first_owner =
        total_pages.any && node_first == total_pages.marked ? previous_street : street;
    const std::uint64_t counted =
        total_pages.add(node_first, (first_byte + rows * row_bytes - 1U) / page_bytes);
    coverage.regret_pages_total[street] += counted;
    if (counted != 0U)
      previous_street = street; // street owning total_pages.marked
    std::uint64_t touched = 0U;
    for (std::uint32_t row = 0; row < rows; ++row) {
      if (slots[row] == 0U)
        continue;
      ++touched;
      const std::uint64_t start = first_byte + row * row_bytes;
      const std::uint64_t first_page = start / page_bytes;
      const bool fresh = !touched_pages.any || first_page > touched_pages.marked;
      std::uint64_t pages = touched_pages.add(first_page, (start + row_bytes - 1U) / page_bytes);
      if (fresh && first_page == node_first && first_owner != street) {
        ++coverage.regret_pages_touched[first_owner];
        --pages;
      }
      coverage.regret_pages_touched[street] += pages;
    }
    coverage.rows_touched[street] += touched;
  }
  return coverage;
}

void Trainer::discount_state(const std::uint64_t iteration) {
  const double t = static_cast<double>(iteration);
  const double positive_power = std::pow(t, config_.dcfr_alpha);
  const double negative_power = std::pow(t, config_.dcfr_beta);
  const double positive_discount = positive_power / (positive_power + 1.0);
  const double negative_discount = negative_power / (negative_power + 1.0);
  const double strategy_discount = std::pow(t / (t + 1.0), config_.dcfr_gamma);
  const auto entries = layout_.entries;
  dispatch_tables(config_.storage, regrets_, strategy_sums_, regrets_f32_, strategy_sums_f32_,
                  [&](auto *regrets, auto *sums) {
                    for (std::uint64_t cell = 0; cell < entries; ++cell) {
                      auto &regret = regrets[cell];
                      const double value = static_cast<double>(regret);
                      store(regret, value * (value > 0.0 ? positive_discount : negative_discount));
                    }
                    for (std::uint64_t cell = 0; cell < entries; ++cell) {
                      auto &sum = sums[cell];
                      store(sum, static_cast<double>(sum) * strategy_discount);
                    }
                  });
}

const double *Trainer::policy_row(const std::uint32_t node, const std::uint16_t hand,
                                  const BoardWork &board) const noexcept {
  const auto &entry = game_->nodes()[node];
  const std::uint64_t slot =
      entry.street == Street::Preflop
          ? board.context.hand_classes()[hand]
          : board.slot[static_cast<std::size_t>(entry.street) - 1U][hand];
  return compact_policy_.data() + compact_offsets_[node] + slot * entry.action_count;
}

card_abstraction::BoardHistory Trainer::sample_history(card_abstraction::DeterministicRandom &random,
                                                      double &weight,
                                                      std::size_t *const index_out) const {
  if (!board_list_.empty()) {
    const auto quantile = random.uniform_unit();
    const auto found = std::lower_bound(board_cumulative_.begin(), board_cumulative_.end(), quantile);
    const auto index = static_cast<std::size_t>(
        std::min<std::ptrdiff_t>(found - board_cumulative_.begin(),
                                 static_cast<std::ptrdiff_t>(board_list_.size()) - 1));
    weight = 1.0;
    if (index_out != nullptr)
      *index_out = index;
    return board_list_[index];
  }
  weight = 1.0;
  if (index_out != nullptr)
    *index_out = board_list_.size();
  return resources_.catalog->sample_physical_history(random);
}

void Trainer::note_board_sample(const card_abstraction::BoardHistory &history,
                                const std::size_t list_index, IterationTelemetry &telemetry) {
  // Exact count of identical board rebuilds: one bit per possible board. With a
  // board list the key is the list index; with catalog sampling it is the flop
  // combination (52 choose 3 = 22,100) times turn times river, 7.5 MB of bits.
  constexpr std::uint64_t card_count = 52U;
  std::uint64_t key = 0U;
  std::uint64_t universe = 0U;
  if (!board_list_.empty()) {
    key = list_index;
    universe = board_list_.size();
  } else {
    std::array<std::uint64_t, 3> flop{history.flop[0].value(), history.flop[1].value(),
                                      history.flop[2].value()};
    std::sort(flop.begin(), flop.end());
    const auto choose2 = [](const std::uint64_t n) { return n * (n - 1U) / 2U; };
    const auto choose3 = [](const std::uint64_t n) { return n * (n - 1U) * (n - 2U) / 6U; };
    const auto flop_index = choose3(flop[2]) + choose2(flop[1]) + flop[0];
    key = (flop_index * card_count + history.turn.value()) * card_count + history.river.value();
    universe = 22'100ULL * card_count * card_count;
  }
  if (board_seen_bits_.empty())
    board_seen_bits_.assign(static_cast<std::size_t>((universe + 63U) / 64U), 0U);
  auto &word = board_seen_bits_[static_cast<std::size_t>(key / 64U)];
  const auto mask = std::uint64_t{1} << (key % 64U);
  if ((word & mask) != 0U) {
    ++boards_repeated_;
    ++telemetry.boards_repeated;
  } else {
    word |= mask;
    ++boards_distinct_;
  }
}

Result<IterationTelemetry, TrainerError> Trainer::iterate() {
  using Outcome = Result<IterationTelemetry, TrainerError>;
  if (!usable_)
    return Outcome::failure(TrainerError::IntegrityFailure);
  const auto started = Clock::now();
  const auto iteration = iteration_ + 1U;
  discount_target_ = iteration - 1U;
  discounts_materialized_ = false;
  if (config_.lazy_discount) {
    prepare_discount_factors(discount_target_);
    // A 16-bit slot holds target - base + 1 <= lazy_discount_epoch; beyond that every
    // touched row is materialized to the target, which becomes the new base.
    if (discount_target_ - discount_epoch_base_ + 1U > config_.lazy_discount_epoch)
      rebase_discount_epoch(discount_target_);
  }
  IterationTelemetry telemetry;
  const auto record_prepare_cpu = [&](const std::vector<BoardWork> &prepared) {
    if (!config_.detailed_profile)
      return;
    for (const auto &work : prepared) {
      telemetry.board_context_cpu_seconds += work.context_cpu_seconds;
      telemetry.all_in_cache_cpu_seconds += work.all_in_cpu_seconds;
      telemetry.reach_setup_cpu_seconds += work.reach_cpu_seconds;
    }
  };
  if (config_.scheme == WeightingScheme::Dcfr && iteration > 1U && !config_.lazy_discount) {
    discount_state(iteration - 1U);
  }
  auto phase = Clock::now();
  telemetry.discount_seconds = std::chrono::duration<double>(phase - started).count();
  const double iteration_weight =
      config_.scheme == WeightingScheme::Linear ? static_cast<double>(iteration) : 1.0;

  auto &batch = board_batch_;
  std::vector<card_abstraction::BoardHistory> histories;
  std::vector<double> weights;
  std::atomic<bool> failed{false};
  const auto prepare_batch = [&]() -> bool {
    failed.store(false, std::memory_order_relaxed);
    executor_->run(batch.size(), [&](const std::size_t index, const unsigned) {
      const auto prepared = prepare_board(histories[index], weights[index], batch[index]);
      if (!prepared)
        failed.store(true, std::memory_order_relaxed);
    });
    record_prepare_cpu(batch);
    return !failed.load(std::memory_order_relaxed);
  };
  const auto draw_batch = [&] {
    for (std::size_t index = 0; index < batch.size(); ++index) {
      double weight = 1.0;
      std::size_t list_index = 0;
      histories[index] = sample_history(training_random_, weight, &list_index);
      weights[index] = weight / config_.batch_boards;
      note_board_sample(histories[index], list_index, telemetry);
    }
  };
  if (!sample_boards_) {
    batch.resize(board_list_.size());
    histories = board_list_;
    weights = board_weights_;
  } else {
    batch.resize(config_.batch_boards);
    histories.resize(batch.size());
    weights.resize(batch.size());
    draw_batch();
  }
  if (!prepare_batch())
    return Outcome::failure(TrainerError::BoardFailure);
  telemetry.board_prepare_seconds = std::chrono::duration<double>(Clock::now() - phase).count();
  for (auto &workspace : workspaces_)
    workspace->reset_counters();
  std::size_t drawn_boards = batch.size();
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    if (hero == 1U && config_.update_mode == UpdateMode::Alternating && sample_boards_) {
      // The opponent's new policy depends on the first batch. Reusing that
      // batch here conditions chance on the policy being evaluated, biasing
      // the second player's counterfactual values. Draw from the unchanged
      // chance law. Exact traversals reuse their weighted list instead.
      phase = Clock::now();
      draw_batch();
      if (!prepare_batch())
        return Outcome::failure(TrainerError::BoardFailure);
      drawn_boards += batch.size();
      telemetry.board_prepare_seconds +=
          std::chrono::duration<double>(Clock::now() - phase).count();
    }
    if (hero == 0U || config_.update_mode == UpdateMode::Alternating) {
      // Snapshot of the current strategy on the rows of this batch, taken
      // before the pass (and, alternating, after the first player's update).
      phase = Clock::now();
      refresh_policy(batch, &telemetry);
      telemetry.policy_refresh_seconds +=
          std::chrono::duration<double>(Clock::now() - phase).count();
    }
    phase = Clock::now();
    for (const auto &work : batch)
      pass(work, hero, iteration_weight, &telemetry);
    telemetry.traversal_seconds += std::chrono::duration<double>(Clock::now() - phase).count();
  }
  iteration_ = iteration;
  boards_processed_ += drawn_boards;
  telemetry.iteration = iteration_;
  telemetry.boards = static_cast<std::uint32_t>(drawn_boards);
  telemetry.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  telemetry.seconds_per_board =
      telemetry.seconds / static_cast<double>(std::max<std::size_t>(1U, drawn_boards));
  for (const auto &workspace : workspaces_) {
    telemetry.nodes_visited += workspace->nodes_visited;
    telemetry.decision_nodes_visited += workspace->decision_nodes_visited;
    telemetry.hero_decision_nodes += workspace->hero_decision_nodes;
    telemetry.opponent_decision_nodes += workspace->opponent_decision_nodes;
    telemetry.chance_nodes_visited += workspace->chance_nodes_visited;
    telemetry.fold_terminals_visited += workspace->fold_terminals_visited;
    telemetry.preflop_all_in_terminals_visited += workspace->preflop_all_in_terminals_visited;
    telemetry.postflop_showdown_terminals_visited +=
        workspace->postflop_showdown_terminals_visited;
    telemetry.zero_reach_prunes += workspace->zero_reach_prunes;
    telemetry.policy_rows_read += workspace->policy_rows_read;
    telemetry.regret_cells_written += workspace->regret_cells_written;
    telemetry.strategy_cells_written += workspace->strategy_cells_written;
    telemetry.sampled_hero_reach_seconds += workspace->sampled_hero_reach_seconds;
    telemetry.sampled_hero_update_seconds += workspace->sampled_hero_update_seconds;
    telemetry.sampled_opponent_reach_seconds += workspace->sampled_opponent_reach_seconds;
    telemetry.sampled_opponent_accumulate_seconds +=
        workspace->sampled_opponent_accumulate_seconds;
    telemetry.sampled_fold_terminal_seconds += workspace->sampled_fold_terminal_seconds;
    telemetry.sampled_preflop_all_in_seconds += workspace->sampled_preflop_all_in_seconds;
    telemetry.sampled_postflop_showdown_seconds +=
        workspace->sampled_postflop_showdown_seconds;
  }
  telemetry.process_bytes = process_working_set_bytes();
  return Outcome::success(telemetry);
}

void Trainer::pass(const BoardWork &board, const std::uint8_t hero, const double iteration_weight,
                   IterationTelemetry *telemetry) {
  const bool profile = config_.detailed_profile && telemetry != nullptr;
  auto phase = Clock::now();
  const auto opponent = static_cast<std::uint8_t>(1U - hero);
  auto &primary = *workspaces_[0];
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double probability = board.hand_probability[hero][hand];
    primary.regret_weight[hand] =
        board.weight * probability * board.pair_probability[hero][hand] * iteration_weight;
    primary.strategy_weight[hand] = board.weight * probability * iteration_weight;
  }
  for (std::size_t thread = 1; thread < workspaces_.size(); ++thread) {
    workspaces_[thread]->regret_weight = primary.regret_weight;
    workspaces_[thread]->strategy_weight = primary.strategy_weight;
  }
  if (profile)
    telemetry->traversal_weight_setup_seconds +=
        std::chrono::duration<double>(Clock::now() - phase).count();

  const double *hero_reach = board.initial_reach[hero].data();
  const double *opponent_reach = board.initial_reach[opponent].data();
  phase = Clock::now();
  top_down_reach(game_->root(), hero_reach, opponent_reach, hero, board, primary, 0U);
  if (profile)
    telemetry->traversal_top_down_seconds +=
        std::chrono::duration<double>(Clock::now() - phase).count();
  phase = Clock::now();
  executor_->run(units_.size(), [&](const std::size_t index, const unsigned thread) {
    auto &unit = units_[index];
    const auto depth = game_->nodes()[unit.root].depth;
    traverse(unit.root, depth, unit.hero_reach.data(), unit.opponent_reach.data(),
             unit.values.data(), hero, board, *workspaces_[thread], false);
  });
  if (profile)
    telemetry->traversal_parallel_seconds +=
        std::chrono::duration<double>(Clock::now() - phase).count();
  phase = Clock::now();
  traverse(game_->root(), 0U, hero_reach, opponent_reach, primary.levels[0].scratch.data(), hero,
           board, primary, true);
  if (profile)
    telemetry->traversal_top_reduce_seconds +=
        std::chrono::duration<double>(Clock::now() - phase).count();
}

void Trainer::top_down_reach(const std::uint32_t node_id, const double *hero_reach,
                             const double *opponent_reach, const std::uint8_t hero,
                             const BoardWork &board, Workspace &workspace,
                             const std::uint32_t depth) {
  const auto unit_index = unit_of_node_[node_id];
  if (unit_index != no_unit) {
    auto &unit = units_[unit_index];
    std::copy_n(hero_reach, live_hand_count, unit.hero_reach.data());
    std::copy_n(opponent_reach, live_hand_count, unit.opponent_reach.data());
    return;
  }
  const auto &node = game_->nodes()[node_id];
  const auto edges = game_->edges_of(node_id);
  if (node.kind == NodeKind::Chance) {
    top_down_reach(edges[0].child, hero_reach, opponent_reach, hero, board, workspace, depth + 1U);
    return;
  }
  if (node.kind != NodeKind::Decision) {
    return;
  }
  auto &level = workspace.levels[depth];
  const double *acting = node.actor == hero ? hero_reach : opponent_reach;
  const auto actions = node.action_count;
  std::uint64_t policy_rows = 0U;
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double reach = acting[hand];
    if (reach == 0.0) {
      for (std::uint8_t action = 0; action < actions; ++action) {
        level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
      }
      continue;
    }
    const auto probabilities = policy_row(node_id, static_cast<std::uint16_t>(hand), board);
    ++policy_rows;
    for (std::uint8_t action = 0; action < actions; ++action) {
      level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] =
          reach * probabilities[action];
    }
  }
  if (config_.detailed_profile)
    workspace.policy_rows_read += policy_rows;
  for (std::uint8_t action = 0; action < actions; ++action) {
    const double *child_reach =
        level.child_reach.data() + static_cast<std::size_t>(action) * live_hand_count;
    if (node.actor == hero) {
      top_down_reach(edges[action].child, child_reach, opponent_reach, hero, board, workspace,
                     depth + 1U);
    } else {
      top_down_reach(edges[action].child, hero_reach, child_reach, hero, board, workspace,
                     depth + 1U);
    }
  }
}

void Trainer::traverse(const std::uint32_t node_id, const std::uint32_t depth,
                       const double *hero_reach, const double *opponent_reach, double *values,
                       const std::uint8_t hero, const BoardWork &board, Workspace &workspace,
                       const bool top_phase) {
  ++workspace.nodes_visited;
  const bool profile_sample =
      config_.detailed_profile && workspace.nodes_visited % profile_sample_stride == 0U;
  if (top_phase) {
    const auto unit_index = unit_of_node_[node_id];
    if (unit_index != no_unit) {
      std::copy_n(units_[unit_index].values.data(), live_hand_count, values);
      return;
    }
  }
  const auto &node = game_->nodes()[node_id];
  const bool opponent_zero = all_zero(opponent_reach);
  if (opponent_zero && all_zero(hero_reach)) {
    if (config_.detailed_profile)
      ++workspace.zero_reach_prunes;
    std::fill_n(values, live_hand_count, 0.0);
    return;
  }
  switch (node.kind) {
  case NodeKind::TerminalFold:
  case NodeKind::TerminalShowdown:
    if (config_.detailed_profile) {
      if (node.kind == NodeKind::TerminalFold)
        ++workspace.fold_terminals_visited;
      else if (node.street == Street::Preflop)
        ++workspace.preflop_all_in_terminals_visited;
      else
        ++workspace.postflop_showdown_terminals_visited;
    }
    if (opponent_zero) {
      std::fill_n(values, live_hand_count, 0.0);
    } else {
      const auto terminal_started = profile_sample ? Clock::now() : Clock::time_point{};
      terminal(node, opponent_reach, values, workspace.levels[depth].scratch.data(), hero, board);
      if (profile_sample) {
        const auto seconds = std::chrono::duration<double>(Clock::now() - terminal_started).count();
        if (node.kind == NodeKind::TerminalFold)
          workspace.sampled_fold_terminal_seconds += seconds;
        else if (node.street == Street::Preflop)
          workspace.sampled_preflop_all_in_seconds += seconds;
        else
          workspace.sampled_postflop_showdown_seconds += seconds;
      }
    }
    return;
  case NodeKind::Chance:
    if (config_.detailed_profile)
      ++workspace.chance_nodes_visited;
    traverse(game_->edges_of(node_id)[0].child, depth + 1U, hero_reach, opponent_reach, values,
             hero, board, workspace, top_phase);
    return;
  case NodeKind::Decision:
    if (config_.detailed_profile) {
      ++workspace.decision_nodes_visited;
      if (node.actor == hero)
        ++workspace.hero_decision_nodes;
      else
        ++workspace.opponent_decision_nodes;
    }
    break;
  }

  auto &level = workspace.levels[depth];
  const auto edges = game_->edges_of(node_id);
  const auto actions = node.action_count;
  if (node.actor == hero) {
    const auto base = layout_.offsets[node_id];
    const auto policy_base = compact_offsets_[node_id];
    const bool preflop = node.street == Street::Preflop;
    const auto street_index = preflop ? 0U : static_cast<std::size_t>(node.street) - 1U;
    std::uint64_t policy_rows = 0U;
    std::uint64_t regret_cells = 0U;
    std::uint64_t strategy_cells = 0U;
    const auto reach_started = profile_sample ? Clock::now() : Clock::time_point{};
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      const auto cell = base +
                        static_cast<std::uint64_t>(board.context.row(
                            node.street, static_cast<std::uint16_t>(hand))) *
                            actions;
      level.cell_offsets[hand] = cell;
      const std::uint64_t slot =
          preflop ? board.context.hand_classes()[hand] : board.slot[street_index][hand];
      const auto policy_offset = policy_base + slot * actions;
      level.policy_offsets[hand] = policy_offset;
      const double reach = hero_reach[hand];
      if (reach == 0.0) {
        for (std::uint8_t action = 0; action < actions; ++action) {
          level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
        }
        continue;
      }
      const double *probabilities = compact_policy_.data() + policy_offset;
      ++policy_rows;
      for (std::uint8_t action = 0; action < actions; ++action) {
        level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] =
            reach * probabilities[action];
      }
    }
    if (profile_sample)
      workspace.sampled_hero_reach_seconds +=
          std::chrono::duration<double>(Clock::now() - reach_started).count();
    for (std::uint8_t action = 0; action < actions; ++action) {
      traverse(edges[action].child, depth + 1U,
               level.child_reach.data() + static_cast<std::size_t>(action) * live_hand_count,
               opponent_reach,
               level.child_values.data() + static_cast<std::size_t>(action) * live_hand_count, hero,
               board, workspace, top_phase);
    }
    const auto update_started = profile_sample ? Clock::now() : Clock::time_point{};
    const bool accumulate_strategy = !fixed_policy_evaluation_;
    dispatch_tables(
        config_.storage, regrets_, strategy_sums_, regrets_f32_, strategy_sums_f32_,
        [&](auto *regrets, auto *sums) {
          const std::size_t update_distance = config_.prefetch_update_hands;
          for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
            if (update_distance != 0U && hand + update_distance < live_hand_count) {
              const auto ahead = level.cell_offsets[hand + update_distance];
              prefetch_read(regrets + ahead);
              prefetch_read(sums + ahead);
            }
            const auto cell = level.cell_offsets[hand];
            const double *probabilities = compact_policy_.data() + level.policy_offsets[hand];
            ++policy_rows;
            double value = 0.0;
            for (std::uint8_t action = 0; action < actions; ++action) {
              value += probabilities[action] *
                       level.child_values[static_cast<std::size_t>(action) * live_hand_count + hand];
            }
            values[hand] = value;
            const double regret_weight = workspace.regret_weight[hand];
            if (!opponent_zero && regret_weight != 0.0) {
              regret_cells += actions;
              for (std::uint8_t action = 0; action < actions; ++action) {
                auto &regret = regrets[cell + action];
                store(regret,
                      static_cast<double>(regret) +
                          regret_weight *
                              (level.child_values[static_cast<std::size_t>(action) *
                                                      live_hand_count +
                                                  hand] -
                               value));
              }
            }
            const double strategy_weight = workspace.strategy_weight[hand] * hero_reach[hand];
            if (accumulate_strategy && strategy_weight != 0.0) {
              strategy_cells += actions;
              for (std::uint8_t action = 0; action < actions; ++action) {
                auto &sum = sums[cell + action];
                store(sum, static_cast<double>(sum) + strategy_weight * probabilities[action]);
              }
            }
          }
        });
    if (profile_sample)
      workspace.sampled_hero_update_seconds +=
          std::chrono::duration<double>(Clock::now() - update_started).count();
    if (config_.detailed_profile) {
      workspace.policy_rows_read += policy_rows;
      workspace.regret_cells_written += regret_cells;
      workspace.strategy_cells_written += strategy_cells;
    }
    return;
  }

  std::uint64_t policy_rows = 0U;
  const auto reach_started = profile_sample ? Clock::now() : Clock::time_point{};
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double reach = opponent_reach[hand];
    if (reach == 0.0) {
      for (std::uint8_t action = 0; action < actions; ++action) {
        level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
      }
      continue;
    }
    const auto probabilities = policy_row(node_id, static_cast<std::uint16_t>(hand), board);
    ++policy_rows;
    for (std::uint8_t action = 0; action < actions; ++action) {
      level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] =
          reach * probabilities[action];
    }
  }
  if (profile_sample)
    workspace.sampled_opponent_reach_seconds +=
        std::chrono::duration<double>(Clock::now() - reach_started).count();
  if (config_.detailed_profile)
    workspace.policy_rows_read += policy_rows;
  std::fill_n(values, live_hand_count, 0.0);
  for (std::uint8_t action = 0; action < actions; ++action) {
    traverse(edges[action].child, depth + 1U, hero_reach,
             level.child_reach.data() + static_cast<std::size_t>(action) * live_hand_count,
             level.child_values.data(), hero, board, workspace, top_phase);
    const auto accumulate_started = profile_sample ? Clock::now() : Clock::time_point{};
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      values[hand] += level.child_values[hand];
    }
    if (profile_sample)
      workspace.sampled_opponent_accumulate_seconds +=
          std::chrono::duration<double>(Clock::now() - accumulate_started).count();
  }
}

void Trainer::all_in_masses(const BoardContext &context, const double *reach, double *win,
                            double *tie, double *lose) const noexcept {
  // Same terms in the same order as the former per-board 465 x 465 copy: the
  // live combos are enumerated in increasing combo id on every board, so the
  // gathered rows reproduce the copied rows element by element.
  constexpr std::size_t stride = card_abstraction::combo_count;
  const ConstHandSpan reach_span(reach, live_hand_count);
  const HandSpan lose_span(lose, live_hand_count);
  fold_mass(context, reach_span, lose_span);
  const auto combos = context.combo_ids();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double *win_row = all_in_win_probability_.data() + combos[hand] * stride;
    const double *tie_row = all_in_tie_probability_.data() + combos[hand] * stride;
    double win_mass = 0.0;
    double tie_mass = 0.0;
    for (std::size_t other = 0; other < live_hand_count; ++other) {
      const auto combo = combos[other];
      win_mass += win_row[combo] * reach[other];
      tie_mass += tie_row[combo] * reach[other];
    }
    win[hand] = win_mass;
    tie[hand] = tie_mass;
    lose[hand] -= win_mass + tie_mass;
  }
}

void Trainer::terminal(const CompiledNode &node, const double *opponent_reach, double *values,
                       double *scratch, const std::uint8_t hero, const BoardWork &board) const {
  const ConstHandSpan reach(opponent_reach, live_hand_count);
  const HandSpan first(scratch, live_hand_count);
  const HandSpan second(scratch + live_hand_count, live_hand_count);
  const HandSpan third(scratch + 2U * live_hand_count, live_hand_count);
  if (node.kind == NodeKind::TerminalFold) {
    fold_mass(board.context, reach, first);
    const double payoff = static_cast<double>(game_->fold_payoffs(node.id)[hero]) * ante_scale;
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      values[hand] = payoff * first[hand];
    }
    return;
  }
  if (node.street == Street::Preflop) {
    if (!all_in_available_) {
      std::fill_n(values, live_hand_count, 0.0);
      return;
    }
    all_in_masses(board.context, opponent_reach, first.data(), second.data(), third.data());
  } else {
    const std::array<ConstHandSpan, 2> reach_by_player{reach, reach};
    kernel_.evaluate(board.context, node.active_mask, hero,
                     std::span<const ConstHandSpan>(reach_by_player.data(), 2U), first, second,
                     third);
  }
  const auto hero_bit = static_cast<std::uint8_t>(std::uint8_t{1} << hero);
  const auto opponents = static_cast<std::uint8_t>(node.active_mask & ~hero_bit);
  const double win = static_cast<double>(game_->showdown_payoffs(node.id, hero_bit)[hero]) * ante_scale;
  const double tie =
      static_cast<double>(game_->showdown_payoffs(node.id, node.active_mask)[hero]) * ante_scale;
  const double lose = static_cast<double>(game_->showdown_payoffs(node.id, opponents)[hero]) * ante_scale;
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    values[hand] = win * first[hand] + tie * second[hand] + lose * third[hand];
  }
}

template <typename Function> void Trainer::for_each_row(Function &&function) const {
  for (const auto &node : game_->nodes()) {
    if (node.kind != NodeKind::Decision) {
      continue;
    }
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    const auto actions = node.action_count;
    const auto base = layout_.offsets[node.id];
    for (std::uint32_t row = 0; row < rows; ++row) {
      function(base + static_cast<std::uint64_t>(row) * actions, actions);
    }
  }
}

void Trainer::average_row(const std::uint64_t offset, const std::uint8_t actions,
                          double *out) const noexcept {
  dispatch_tables(config_.storage, regrets_, strategy_sums_, regrets_f32_, strategy_sums_f32_,
                  [&](const auto *regrets, const auto *sums) {
                    double total = 0.0;
                    for (std::uint8_t action = 0; action < actions; ++action) {
                      total += std::max(0.0, static_cast<double>(sums[offset + action]));
                    }
                    if (total > 0.0) {
                      for (std::uint8_t action = 0; action < actions; ++action) {
                        out[action] =
                            std::max(0.0, static_cast<double>(sums[offset + action])) / total;
                      }
                      return;
                    }
                    double positive = 0.0;
                    for (std::uint8_t action = 0; action < actions; ++action) {
                      positive += std::max(0.0, static_cast<double>(regrets[offset + action]));
                    }
                    for (std::uint8_t action = 0; action < actions; ++action) {
                      out[action] =
                          positive > 0.0
                              ? std::max(0.0, static_cast<double>(regrets[offset + action])) /
                                    positive
                              : 1.0 / actions;
                    }
                  });
}

void Trainer::current_row(const std::uint64_t offset, const std::uint8_t actions,
                          double *out) const noexcept {
  dispatch_tables(config_.storage, regrets_, strategy_sums_, regrets_f32_, strategy_sums_f32_,
                  [&](const auto *regrets, const auto *) {
                    double positive = 0.0;
                    for (std::uint8_t action = 0; action < actions; ++action) {
                      positive += std::max(0.0, static_cast<double>(regrets[offset + action]));
                    }
                    for (std::uint8_t action = 0; action < actions; ++action) {
                      out[action] =
                          positive > 0.0
                              ? std::max(0.0, static_cast<double>(regrets[offset + action])) /
                                    positive
                              : 1.0 / actions;
                    }
                  });
}

void Trainer::fill_average_policy(std::vector<double> &table) {
  if (!usable_)
    throw std::logic_error("trainer state invalid after failed checkpoint load");
  materialize_all_discounts();
  table.resize(layout_.entries);
  for_each_row([&](const std::uint64_t offset, const std::uint8_t actions) {
    average_row(offset, actions, table.data() + offset);
  });
}

void Trainer::fill_current_policy(std::vector<double> &table) {
  if (!usable_)
    throw std::logic_error("trainer state invalid after failed checkpoint load");
  materialize_all_discounts();
  table.resize(layout_.entries);
  for_each_row([&](const std::uint64_t offset, const std::uint8_t actions) {
    current_row(offset, actions, table.data() + offset);
  });
}

BucketPolicy Trainer::average_policy() {
  std::vector<double> table;
  fill_average_policy(table);
  return BucketPolicy(*game_, layout_, std::move(table));
}

BucketPolicy Trainer::current_policy() {
  std::vector<double> table;
  fill_current_policy(table);
  return BucketPolicy(*game_, layout_, std::move(table));
}

Result<std::string, TrainerError> Trainer::save_average_policy(const std::filesystem::path &path,
                                                               const std::string &source) {
  using Outcome = Result<std::string, TrainerError>;
  if (!usable_)
    return Outcome::failure(TrainerError::IntegrityFailure);
  materialize_all_discounts();
  PolicyStreamWriter writer(path, *game_, layout_, source);
  if (!writer.ok())
    return Outcome::failure(TrainerError::IoFailure);
  std::vector<double> block;
  block.reserve(export_block_entries + maximum_actions);
  std::array<double, maximum_actions> row{};
  for_each_row([&](const std::uint64_t offset, const std::uint8_t actions) {
    average_row(offset, actions, row.data());
    block.insert(block.end(), row.begin(), row.begin() + actions);
    if (block.size() >= export_block_entries) {
      writer.append(block);
      block.clear();
    }
  });
  writer.append(block);
  const auto finished = writer.finish();
  if (!finished)
    return Outcome::failure(TrainerError::IoFailure);
  return Outcome::success(finished.value());
}

Result<std::string, TrainerError> Trainer::save_current_policy(const std::filesystem::path &path,
                                                               const std::string &source) {
  using Outcome = Result<std::string, TrainerError>;
  if (!usable_)
    return Outcome::failure(TrainerError::IntegrityFailure);
  materialize_all_discounts();
  PolicyStreamWriter writer(path, *game_, layout_, source);
  if (!writer.ok())
    return Outcome::failure(TrainerError::IoFailure);
  std::vector<double> block;
  block.reserve(export_block_entries + maximum_actions);
  std::array<double, maximum_actions> row{};
  for_each_row([&](const std::uint64_t offset, const std::uint8_t actions) {
    current_row(offset, actions, row.data());
    block.insert(block.end(), row.begin(), row.begin() + actions);
    if (block.size() >= export_block_entries) {
      writer.append(block);
      block.clear();
    }
  });
  writer.append(block);
  const auto finished = writer.finish();
  if (!finished)
    return Outcome::failure(TrainerError::IoFailure);
  return Outcome::success(finished.value());
}

double Trainer::regret(const std::uint64_t cell) const noexcept {
  return config_.storage == TableStorage::Float32 ? static_cast<double>(regrets_f32_[cell])
                                                  : regrets_[cell];
}

double Trainer::strategy_sum(const std::uint64_t cell) const noexcept {
  return config_.storage == TableStorage::Double ? strategy_sums_[cell]
                                                 : static_cast<double>(strategy_sums_f32_[cell]);
}

std::vector<double> Trainer::regrets() const {
  if (config_.storage != TableStorage::Float32)
    return regrets_;
  return std::vector<double>(regrets_f32_.begin(), regrets_f32_.end());
}

std::vector<double> Trainer::strategy_sums() const {
  if (config_.storage == TableStorage::Double)
    return strategy_sums_;
  return std::vector<double>(strategy_sums_f32_.begin(), strategy_sums_f32_.end());
}

std::uint64_t Trainer::regret_table_bytes() const noexcept {
  return config_.storage == TableStorage::Float32 ? regrets_f32_.size() * sizeof(float)
                                                  : regrets_.size() * sizeof(double);
}

std::uint64_t Trainer::strategy_table_bytes() const noexcept {
  return config_.storage == TableStorage::Double ? strategy_sums_.size() * sizeof(double)
                                                 : strategy_sums_f32_.size() * sizeof(float);
}

const char *Trainer::regret_table_data() const noexcept {
  return config_.storage == TableStorage::Float32
             ? reinterpret_cast<const char *>(regrets_f32_.data())
             : reinterpret_cast<const char *>(regrets_.data());
}

const char *Trainer::strategy_table_data() const noexcept {
  return config_.storage == TableStorage::Double
             ? reinterpret_cast<const char *>(strategy_sums_.data())
             : reinterpret_cast<const char *>(strategy_sums_f32_.data());
}

void Trainer::add_regret(const std::uint64_t cell, const double increment) noexcept {
  if (config_.storage == TableStorage::Float32)
    store(regrets_f32_[cell], static_cast<double>(regrets_f32_[cell]) + increment);
  else
    regrets_[cell] += increment;
}

std::uint64_t Trainer::state_bytes() const noexcept {
  return regret_table_bytes() + strategy_table_bytes() +
         compact_policy_.capacity() * sizeof(double) +
         discount_iterations_.size() * sizeof(std::uint16_t) +
         (positive_discount_prefix_.size() + strategy_discount_prefix_.size()) * sizeof(double);
}

MemoryBreakdown Trainer::memory_breakdown() const noexcept {
  MemoryBreakdown breakdown;
  breakdown.regret_bytes =
      regrets_.capacity() * sizeof(double) + regrets_f32_.capacity() * sizeof(float);
  breakdown.strategy_sum_bytes =
      strategy_sums_.capacity() * sizeof(double) + strategy_sums_f32_.capacity() * sizeof(float);
  breakdown.regret_bytes_per_cell = config_.storage == TableStorage::Float32 ? 4U : 8U;
  breakdown.strategy_sum_bytes_per_cell = config_.storage == TableStorage::Double ? 8U : 4U;
  breakdown.compact_policy_capacity_bytes = compact_policy_.capacity() * sizeof(double);
  breakdown.compact_policy_offsets_bytes = compact_offsets_.capacity() * sizeof(std::uint64_t);
  breakdown.discount_timestamp_bytes = discount_iterations_.capacity() * sizeof(std::uint16_t);
  breakdown.discount_factor_bytes =
      (positive_discount_prefix_.capacity() + strategy_discount_prefix_.capacity()) *
      sizeof(double);
  breakdown.discount_offset_bytes = discount_offsets_.capacity() * sizeof(std::uint64_t);
  breakdown.all_in_dense_bytes =
      (all_in_win_probability_.capacity() + all_in_tie_probability_.capacity()) * sizeof(double);
  breakdown.board_batch_bytes = 0U;
  for (const auto &work : board_batch_)
    breakdown.board_batch_bytes += work.bytes();
  breakdown.board_batch_bytes +=
      (board_batch_.capacity() - board_batch_.size()) * sizeof(BoardWork);
  for (const auto &workspace : workspaces_)
    breakdown.workspace_bytes += workspace->bytes();
  for (const auto &unit : units_)
    breakdown.unit_bytes +=
        sizeof(Unit) +
        (unit.hero_reach.capacity() + unit.opponent_reach.capacity() + unit.values.capacity()) *
            sizeof(double);
  breakdown.layout_offset_bytes = layout_.offsets.capacity() * sizeof(std::uint64_t);
  breakdown.partition_bytes = partition_.unit_roots.capacity() * sizeof(std::uint32_t) +
                              partition_.is_top.capacity() + unit_of_node_.capacity() * 4U;
  breakdown.board_list_bytes =
      board_list_.capacity() * sizeof(card_abstraction::BoardHistory) +
      (board_weights_.capacity() + board_cumulative_.capacity()) * sizeof(double) +
      board_seen_bits_.capacity() * sizeof(std::uint64_t);
  breakdown.hand_mask_bytes = hand_masks_[0].capacity() + hand_masks_[1].capacity();
  breakdown.tree_bytes = game_->nodes().size() * sizeof(CompiledNode) +
                         game_->edges().size() * sizeof(CompiledEdge) +
                         game_->states().size() * sizeof(PublicState);
  breakdown.history_map_resident_bytes =
      resources_.history_rows ? resources_.history_rows->resident_byte_size() : 0U;
  for (const auto *table : {resources_.flop, resources_.turn, resources_.river})
    if (table != nullptr)
      breakdown.bucket_table_bytes += table->payload_bytes();
  breakdown.rank_table_bytes = resources_.ranks ? resources_.ranks->payload_bytes() : 0U;
  breakdown.catalog_bytes = resources_.catalog ? resources_.catalog->byte_size() : 0U;
  breakdown.all_in_table_bytes = resources_.all_in ? resources_.all_in->payload_bytes() : 0U;
  breakdown.cells_by_street = layout_.entries_by_street;
  for (const auto &node : game_->nodes())
    if (node.kind == NodeKind::Decision)
      breakdown.rows_by_street[static_cast<std::size_t>(node.street)] += StateLayout::rows_for(
          node.street, config_.flop_capacity, config_.turn_capacity, config_.river_capacity);
  return breakdown;
}

Result<ExploitabilityEstimate, TrainerError>
Trainer::estimate_exploitability(const std::uint32_t flops, const bool exact_on_list) {
  using Outcome = Result<ExploitabilityEstimate, TrainerError>;
  const auto started = Clock::now();
  if ((!board_list_.empty() || subsets_) && !supported_diagnostic_prior(board_list_, hand_masks_)) {
    // A restricted board corpus can change the preflop private-deal prior.
    // The physical evaluator assumes a fixed preflop deal, so labeling its
    // result exact for that different game would be incorrect.
    return Outcome::failure(TrainerError::UnsupportedBoardPrior);
  }
  const bool exact = !board_list_.empty() && (!sample_boards_ || exact_on_list);
  std::vector<FlopGroup> groups;
  if (exact) {
    std::vector<WeightedBoard> boards;
    for (std::size_t index = 0; index < board_list_.size(); ++index) {
      boards.push_back({board_list_[index], board_weights_[index]});
    }
    groups = group_by_flop(boards);
  } else if (!board_list_.empty()) {
    if (flops == 0U) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
    std::vector<WeightedBoard> boards;
    for (std::uint32_t draw = 0; draw < flops; ++draw) {
      double weight = 1.0;
      boards.push_back({sample_history(evaluation_random_, weight), 1.0});
    }
    groups = group_by_flop(boards);
  } else {
    if (flops == 0U) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
    for (std::uint32_t draw = 0; draw < flops; ++draw) {
      double weight = 1.0;
      const auto history = sample_history(evaluation_random_, weight);
      groups.push_back(full_runouts(history.flop));
    }
  }
  BestResponseResources resources;
  resources.ranks = resources_.ranks;
  resources.all_in = resources_.all_in;
  resources.catalog = resources_.catalog;
  resources.flop = resources_.flop;
  resources.turn = resources_.turn;
  resources.river = resources_.river;
  resources.class_rows = resources_.class_rows;
  resources.history_rows = resources_.history_rows;
  BestResponseOptions options;
  options.threads = config_.evaluation_threads == 0U ? config_.threads
                                                     : config_.evaluation_threads;
  if (subsets_) {
    for (std::uint8_t player = 0; player < 2U; ++player) {
      for (std::uint16_t combo = 0; combo < 630U; ++combo) {
        if (hand_masks_[player][combo] != 0U) {
          options.hand_subsets[player].push_back(combo);
        }
      }
    }
  }
  const auto average = average_policy();
  const auto report = evaluate_best_response(*game_, average, resources, groups, options);
  if (!report) {
    return Outcome::failure(TrainerError::BoardFailure);
  }
  ExploitabilityEstimate estimate;
  estimate.flops = report.value().flops;
  estimate.boards = report.value().boards;
  estimate.exact = exact;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    estimate.ev[player] = report.value().ev[player];
    estimate.best_response[player] = report.value().best_response[player];
    estimate.gain[player] = report.value().gain[player];
    estimate.gain_lower[player] = report.value().gain_lower[player];
    const auto response_error = report.value().best_response_standard_error[player];
    const auto ev_error = report.value().ev_standard_error[player];
    estimate.gain_standard_error[player] =
        exact ? 0.0 : std::sqrt(response_error * response_error + ev_error * ev_error);
  }
  const auto worst = estimate.gain[0] >= estimate.gain[1] ? 0U : 1U;
  estimate.max_gain = estimate.gain[worst];
  estimate.max_gain_lower = report.value().max_gain_lower;
  estimate.max_gain_half_width = 1.96 * estimate.gain_standard_error[worst];
  estimate.nashconv = estimate.gain[0] + estimate.gain[1];
  estimate.normalized_dev = initial_pot_antes_ > 0.0 ? estimate.max_gain / initial_pot_antes_ : 0.0;
  estimate.normalized_stack = stack_antes_ > 0.0 ? estimate.max_gain / stack_antes_ : 0.0;
  estimate.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  return Outcome::success(estimate);
}

std::string Trainer::state_fingerprint() {
  if (!usable_)
    throw std::logic_error("trainer state invalid after failed checkpoint load");
  materialize_all_discounts();
  auto hash = detail::fnv1a_text(identity_);
  std::string header;
  append_little(header, iteration_);
  append_little(header, boards_processed_);
  for (const auto word : training_random_.state()) {
    append_little(header, word);
  }
  for (const auto word : evaluation_random_.state()) {
    append_little(header, word);
  }
  hash = detail::fnv1a_text(header, hash);
  const std::string_view regrets(regret_table_data(), regret_table_bytes());
  const std::string_view sums(strategy_table_data(), strategy_table_bytes());
  hash = detail::fnv1a_text(regrets, hash);
  hash = detail::fnv1a_text(sums, hash);
  return "fnv1a64:" + detail::hex64_text(hash);
}

Result<bool, TrainerError> Trainer::save_checkpoint(const std::filesystem::path &path) {
  using Outcome = Result<bool, TrainerError>;
  if (!usable_)
    return Outcome::failure(TrainerError::IntegrityFailure);
  materialize_all_discounts();
  std::string buffer;
  buffer.append(checkpoint_magic.data(), checkpoint_magic.size());
  append_little32(buffer, checkpoint_version);
  append_little32(buffer, static_cast<std::uint32_t>(identity_.size()));
  buffer += identity_;
  append_little(buffer, iteration_);
  append_little(buffer, boards_processed_);
  for (const auto word : training_random_.state()) {
    append_little(buffer, word);
  }
  for (const auto word : evaluation_random_.state()) {
    append_little(buffer, word);
  }
  append_little(buffer, layout_.entries);

  const auto temporary = std::filesystem::path(path.string() + ".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Outcome::failure(TrainerError::IoFailure);
    }
    const std::array<std::string_view, 2> arrays{
        std::string_view(regret_table_data(), regret_table_bytes()),
        std::string_view(strategy_table_data(), strategy_table_bytes())};
    if (!stream_io::write_raw(output, buffer, arrays)) {
      return Outcome::failure(TrainerError::IoFailure);
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return Outcome::failure(TrainerError::IoFailure);
  }
  return Outcome::success(true);
}

Result<bool, TrainerError> Trainer::load_checkpoint(const std::filesystem::path &path) {
  using Outcome = Result<bool, TrainerError>;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Outcome::failure(TrainerError::IoFailure);
  }
  std::uint64_t iteration = 0U;
  std::uint64_t boards_processed = 0U;
  card_abstraction::DeterministicRandom::State training_state{};
  card_abstraction::DeterministicRandom::State evaluation_state{};
  const bool float_regrets = config_.storage == TableStorage::Float32;
  const bool float_sums = config_.storage != TableStorage::Double;
  const std::uint64_t regret_cell_bytes = float_regrets ? sizeof(float) : sizeof(double);
  const std::uint64_t sum_cell_bytes = float_sums ? sizeof(float) : sizeof(double);
  const auto read_header = [&](stream_io::Reader &body) -> std::optional<TrainerError> {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::string identity;
    std::uint64_t entries = 0;
    if (!body.bytes(magic.data(), magic.size()) || magic != checkpoint_magic || !body.u32(version))
      return TrainerError::IntegrityFailure;
    if (version != checkpoint_version)
      return TrainerError::UnsupportedVersion;
    if (!body.string(identity) || identity != identity_ || !body.u64(iteration) ||
        !body.u64(boards_processed))
      return TrainerError::IntegrityFailure;
    for (auto &word : training_state)
      if (!body.u64(word))
        return TrainerError::IntegrityFailure;
    for (auto &word : evaluation_state)
      if (!body.u64(word))
        return TrainerError::IntegrityFailure;
    if (!body.u64(entries) || entries != layout_.entries ||
        body.remaining() != entries * (regret_cell_bytes + sum_cell_bytes))
      return TrainerError::IntegrityFailure;
    return std::nullopt;
  };
  stream_io::Reader validation(input);
  const auto header_status = read_header(validation);
  if (header_status)
    return Outcome::failure(*header_status);
  const bool scanned = (float_regrets ? validation.scan_floats(layout_.entries)
                                      : validation.scan_doubles(layout_.entries)) &&
                       (float_sums ? validation.scan_floats(layout_.entries, true)
                                   : validation.scan_doubles(layout_.entries, true)) &&
                       validation.finish();
  if (!scanned)
    return Outcome::failure(TrainerError::IntegrityFailure);
  // Validate the complete file before changing existing state. The second
  // pass uses the already allocated arrays. A changed file or an I/O failure
  // during this commit poisons the trainer until another successful load.
  stream_io::Reader commit(input);
  if (read_header(commit))
    return Outcome::failure(TrainerError::IntegrityFailure);
  usable_ = false;
  const bool committed = (float_regrets ? commit.floats(regrets_f32_) : commit.doubles(regrets_)) &&
                         (float_sums ? commit.floats(strategy_sums_f32_, true)
                                     : commit.doubles(strategy_sums_, true)) &&
                         commit.finish();
  if (!committed)
    return Outcome::failure(TrainerError::IntegrityFailure);
  iteration_ = iteration;
  boards_processed_ = boards_processed;
  discounts_materialized_ = false;
  training_random_.restore(training_state);
  evaluation_random_.restore(evaluation_state);
  discount_target_ = iteration_ > 0U ? iteration_ - 1U : 0U;
  if (config_.lazy_discount) {
    // A checkpoint holds every row materialized to its iteration. The epoch base is the
    // one a continuous run has at this target (a multiple of the epoch), so later rebases
    // fall on the same iterations and a resume stays bit-identical; every slot marks its
    // row as touched (the coverage is unknown after a load).
    const std::uint64_t epoch = config_.lazy_discount_epoch;
    discount_epoch_base_ = discount_target_ - discount_target_ % epoch;
    std::fill(discount_iterations_.begin(), discount_iterations_.end(),
              static_cast<std::uint16_t>(discount_target_ - discount_epoch_base_ + 1U));
    prepare_discount_factors(discount_target_);
  }
  usable_ = true;
  return Outcome::success(true);
}

class TrainerAccess {
public:
  static Result<AbstractBestResponseReport, TrainerError>
  abstract_best_response(const CompiledGame &game, BucketPolicy policy,
                         const TrainerResources &resources, const std::vector<FlopGroup> &groups,
                         const AbstractBestResponseOptions &options) {
    using Outcome = Result<AbstractBestResponseReport, TrainerError>;
    const auto started = Clock::now();
    if (options.threads == 0U || groups.empty() || resources.history_rows == nullptr)
      return Outcome::failure(TrainerError::InvalidConfiguration);
    const auto &source_layout = policy.layout();
    if (source_layout.entries != policy.table().size())
      return Outcome::failure(TrainerError::InvalidConfiguration);

    double group_weight = 0.0;
    for (const auto &group : groups) {
      if (!(group.weight > 0.0) || !std::isfinite(group.weight) || group.boards.empty())
        return Outcome::failure(TrainerError::InvalidConfiguration);
      group_weight += group.weight;
      double board_weight = 0.0;
      for (const auto &board : group.boards) {
        if (!(board.weight > 0.0) || !std::isfinite(board.weight))
          return Outcome::failure(TrainerError::InvalidConfiguration);
        board_weight += board.weight;
      }
      if (!(board_weight > 0.0) || !std::isfinite(board_weight))
        return Outcome::failure(TrainerError::InvalidConfiguration);
    }
    if (!(group_weight > 0.0) || !std::isfinite(group_weight))
      return Outcome::failure(TrainerError::InvalidConfiguration);

    TrainerConfig config;
    config.flop_capacity = source_layout.flop_capacity;
    config.turn_capacity = source_layout.turn_capacity;
    config.river_capacity = source_layout.river_capacity;
    config.batch_boards = 1U;
    config.threads = options.threads;
    std::vector<double> fixed_policy = std::move(policy.table());
    std::unique_ptr<Trainer> evaluator(new Trainer(game, resources, config));
    HandSubsets subsets;
    subsets.combos = options.hand_subsets;
    const bool restricted = !subsets.combos[0].empty() || !subsets.combos[1].empty();
    const auto initialized =
        evaluator->initialize(nullptr, restricted ? &subsets : nullptr, &fixed_policy);
    if (!initialized)
      return Outcome::failure(initialized.error());

    AbstractBestResponseReport report;
    report.flops = static_cast<std::uint32_t>(groups.size());
    Trainer::BoardWork work;
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
      const auto &group = groups[group_index];
      const double normalized_group = group.weight / group_weight;
      const double board_total = std::accumulate(
          group.boards.begin(), group.boards.end(), 0.0,
          [](const double sum, const WeightedBoard &board) { return sum + board.weight; });
      for (const auto &board : group.boards) {
        const auto prepared = evaluator->prepare_board(
            board.history, normalized_group * board.weight / board_total, work);
        if (!prepared)
          return Outcome::failure(prepared.error());
        evaluator->pass(work, 0U, 1.0);
        evaluator->pass(work, 1U, 1.0);
        ++report.boards;
      }
      if (options.progress) {
        AbstractBestResponseProgress progress;
        progress.flops_done = static_cast<std::uint32_t>(group_index + 1U);
        progress.flops_total = report.flops;
        progress.boards_done = report.boards;
        progress.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        options.progress(progress);
      }
    }

    struct Predecessor {
      std::uint32_t node{no_node};
      std::uint8_t action{0U};
    };
    std::vector<Predecessor> predecessors(game.nodes().size());
    for (const auto &node : game.nodes()) {
      if (node.kind != NodeKind::Decision)
        continue;
      auto child = node.id;
      auto parent = node.parent;
      while (parent != no_node) {
        const auto &ancestor = game.nodes()[parent];
        if (ancestor.kind == NodeKind::Decision && ancestor.actor == node.actor) {
          const auto edges = game.edges_of(parent);
          const auto edge = std::find_if(edges.begin(), edges.end(),
                                         [&](const CompiledEdge &entry) {
                                           return entry.child == child;
                                         });
          if (edge == edges.end())
            return Outcome::failure(TrainerError::IntegrityFailure);
          predecessors[node.id] =
              {parent, static_cast<std::uint8_t>(edge - edges.begin())};
          break;
        }
        child = parent;
        parent = ancestor.parent;
      }
    }

    const auto predecessor_row = [&](const Street street, const std::uint32_t row,
                                     const Street target) {
      auto current_street = street;
      auto current_row = row;
      while (current_street != target && current_row != no_history_row) {
        if (current_street == Street::Preflop)
          return no_history_row;
        current_row = resources.history_rows->parent_row(current_street, current_row);
        current_street = static_cast<Street>(static_cast<std::uint8_t>(current_street) - 1U);
      }
      return current_street == target ? current_row : no_history_row;
    };

    for (const std::uint8_t hero : {std::uint8_t{0}, std::uint8_t{1}}) {
      double gain = 0.0;
      for (auto iterator = game.nodes().rbegin(); iterator != game.nodes().rend(); ++iterator) {
        const auto &node = *iterator;
        if (node.kind != NodeKind::Decision || node.actor != hero)
          continue;
        const auto rows = StateLayout::rows_for(node.street, source_layout.flop_capacity,
                                                source_layout.turn_capacity,
                                                source_layout.river_capacity);
        const auto &predecessor = predecessors[node.id];
        for (std::uint32_t row = 0; row < rows; ++row) {
          const auto offset = source_layout.offsets[node.id] +
                              static_cast<std::uint64_t>(row) * node.action_count;
          double value = -std::numeric_limits<double>::infinity();
          for (std::uint8_t action = 0; action < node.action_count; ++action)
            value = std::max(value, evaluator->regret(offset + action));
          if (!std::isfinite(value))
            return Outcome::failure(TrainerError::IntegrityFailure);
          if (predecessor.node == no_node) {
            gain += value;
            continue;
          }
          const auto &parent = game.nodes()[predecessor.node];
          const auto parent_row = predecessor_row(node.street, row, parent.street);
          if (parent_row == no_history_row)
            return Outcome::failure(TrainerError::IntegrityFailure);
          const auto parent_offset =
              source_layout.offsets[parent.id] +
              static_cast<std::uint64_t>(parent_row) * parent.action_count + predecessor.action;
          evaluator->add_regret(parent_offset, value);
        }
      }
      if (!std::isfinite(gain) || gain < -1e-9)
        return Outcome::failure(TrainerError::IntegrityFailure);
      report.gain[hero] = std::max(0.0, gain);
    }
    report.max_gain = std::max(report.gain[0], report.gain[1]);
    report.process_bytes = process_working_set_bytes();
    report.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return Outcome::success(report);
  }
};

Result<AbstractBestResponseReport, TrainerError>
evaluate_abstract_best_response(const CompiledGame &game, BucketPolicy policy,
                                const TrainerResources &resources,
                                const std::vector<FlopGroup> &groups,
                                const AbstractBestResponseOptions &options) {
  return TrainerAccess::abstract_best_response(game, std::move(policy), resources, groups, options);
}

std::uint64_t process_working_set_bytes() noexcept {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
  }
#endif
  return 0U;
}

ProcessMemoryPeaks process_memory_peaks() noexcept {
  ProcessMemoryPeaks peaks;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                           sizeof(counters)) != 0) {
    peaks.working_set_bytes = counters.WorkingSetSize;
    peaks.peak_working_set_bytes = counters.PeakWorkingSetSize;
    peaks.private_commit_bytes = counters.PagefileUsage;
    peaks.peak_private_commit_bytes = counters.PeakPagefileUsage;
    peaks.page_faults = counters.PageFaultCount;
  }
#endif
  return peaks;
}

const char *trainer_error_name(const TrainerError error) noexcept {
  switch (error) {
  case TrainerError::InvalidConfiguration:
    return "invalid_configuration";
  case TrainerError::MissingResource:
    return "missing_resource";
  case TrainerError::BoardFailure:
    return "board_failure";
  case TrainerError::IoFailure:
    return "io_failure";
  case TrainerError::IntegrityFailure:
    return "integrity_failure";
  case TrainerError::UnsupportedVersion:
    return "unsupported_version";
  case TrainerError::UnsupportedBoardPrior:
    return "unsupported_board_prior";
  }
  return "unknown";
}

const char *weighting_scheme_name(const WeightingScheme scheme) noexcept {
  return scheme == WeightingScheme::Linear ? "linear" : "dcfr";
}

const char *update_mode_name(const UpdateMode mode) noexcept {
  return mode == UpdateMode::Simultaneous ? "simultaneous" : "alternating";
}

const char *table_storage_name(const TableStorage storage) noexcept {
  switch (storage) {
  case TableStorage::Double:
    return "double";
  case TableStorage::MixedFloatSums:
    return "mixed-float32-sums";
  case TableStorage::Float32:
    return "float32";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
