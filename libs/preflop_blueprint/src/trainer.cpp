#include "gtosd/preflop_blueprint/trainer.hpp"
#include "gtosd/preflop_blueprint/abstract_best_response.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"

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
};

struct Trainer::BoardWork {
  BoardContext context;
  AllInEquityCache all_in;
  bool has_all_in{false};
  double weight{1.0};
  std::array<std::vector<double>, 2> initial_reach;
  std::array<std::vector<double>, 2> hand_probability;
  std::array<std::vector<double>, 2> pair_probability;
  double context_cpu_seconds{0.0};
  double all_in_cpu_seconds{0.0};
  double reach_cpu_seconds{0.0};
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
  if (config.reuse_discount_invariant_policy &&
      (!config.lazy_discount || config.scheme != WeightingScheme::Dcfr ||
       config.update_mode != UpdateMode::Alternating || config.dcfr_beta != 0.0)) {
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
    if (capacity(*resources_.flop) != config_.flop_capacity ||
        capacity(*resources_.turn) != config_.turn_capacity ||
        capacity(*resources_.river) != config_.river_capacity) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
  }
  if (stats.preflop_all_in_runouts > 0U && resources_.all_in == nullptr) {
    return Outcome::failure(TrainerError::MissingResource);
  }
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
    discount_iterations_.assign(static_cast<std::size_t>(discount_rows), 0U);
  }
  fixed_policy_evaluation_ = fixed_policy != nullptr;
  if (fixed_policy_evaluation_) {
    if (fixed_policy->size() != layout_.entries)
      return Outcome::failure(TrainerError::InvalidConfiguration);
    policy_ = std::move(*fixed_policy);
  } else {
    strategy_sums_.assign(layout_.entries, 0.0);
    policy_.assign(layout_.entries, 0.0);
    refresh_policy();
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
  if (config_.reuse_discount_invariant_policy)
    identity += "|reuse-discount-invariant-policy-v1";
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
  work.has_all_in = false;
  if (game_->stats().preflop_all_in_runouts > 0U && resources_.all_in != nullptr) {
    const auto rebuilt = work.all_in.rebuild(work.context, all_in_win_probability_,
                                             all_in_tie_probability_);
    if (!rebuilt) {
      return Outcome::failure(TrainerError::BoardFailure);
    }
    work.has_all_in = true;
  }
  if (config_.detailed_profile) {
    const auto now = Clock::now();
    work.all_in_cpu_seconds = std::chrono::duration<double>(now - phase).count();
    phase = now;
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

void Trainer::refresh_policy(const std::vector<BoardWork> *batch, const int actor,
                             const ActiveRows *prepared_rows) {
  policy_.resize(layout_.entries);
  ActiveRows owned_rows;
  if (batch != nullptr && prepared_rows == nullptr)
    owned_rows = collect_active_rows(*batch);
  const auto *active =
      prepared_rows != nullptr ? prepared_rows : (batch != nullptr ? &owned_rows : nullptr);
  executor_->run(game_->nodes().size(), [&](const std::size_t node_index, const unsigned) {
    const auto &node = game_->nodes()[node_index];
    if (node.kind != NodeKind::Decision ||
        (actor >= 0 && node.actor != static_cast<std::uint8_t>(actor))) {
      return;
    }
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    const auto actions = node.action_count;
    const auto base = layout_.offsets[node.id];
    const auto &selected = active != nullptr ? (*active)[static_cast<std::size_t>(node.street)]
                                             : owned_rows[0];
    const auto count = active != nullptr ? selected.size() : rows;
    for (std::size_t index = 0; index < count; ++index) {
      const auto row = active != nullptr ? selected[index] : static_cast<std::uint32_t>(index);
      materialize_row(node.id, row, discount_target_);
      const auto offset = base + static_cast<std::uint64_t>(row) * actions;
      double positive = 0.0;
      for (std::uint8_t action = 0; action < actions; ++action) {
        positive += std::max(0.0, regrets_[offset + action]);
      }
      if (positive <= 0.0) {
        const double uniform = 1.0 / actions;
        for (std::uint8_t action = 0; action < actions; ++action) {
          policy_[offset + action] = uniform;
        }
      } else {
        for (std::uint8_t action = 0; action < actions; ++action) {
          policy_[offset + action] = std::max(0.0, regrets_[offset + action]) / positive;
        }
      }
    }
  });
}

void Trainer::materialize_active_rows(const ActiveRows &active, const std::uint8_t actor) {
  if (!config_.lazy_discount)
    return;
  executor_->run(game_->nodes().size(), [&](const std::size_t node_index, const unsigned) {
    const auto &node = game_->nodes()[node_index];
    if (node.kind != NodeKind::Decision || node.actor != actor)
      return;
    for (const auto row : active[static_cast<std::size_t>(node.street)])
      materialize_row(node.id, row, discount_target_);
  });
}

void Trainer::materialize_row(const std::uint32_t node_id, const std::uint32_t row,
                              const std::uint64_t iteration) {
  if (!config_.lazy_discount)
    return;
  if (iteration > std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("lazy discount iteration overflow");
  const auto timestamp_index = discount_offsets_[node_id] + row;
  auto &last = discount_iterations_[static_cast<std::size_t>(timestamp_index)];
  if (last >= iteration)
    return;
  const auto &node = game_->nodes()[node_id];
  const auto offset = layout_.offsets[node_id] +
                      static_cast<std::uint64_t>(row) * node.action_count;
  bool nonzero = false;
  for (std::uint8_t action = 0; action < node.action_count; ++action) {
    nonzero = nonzero || regrets_[offset + action] != 0.0 ||
              strategy_sums_[offset + action] != 0.0;
  }
  if (nonzero) {
    const auto first = static_cast<std::size_t>(last);
    const auto final = static_cast<std::size_t>(iteration);
    const auto skipped = iteration - last;
    const double strategy = strategy_discount_prefix_[final] / strategy_discount_prefix_[first];
    for (std::uint8_t action = 0; action < node.action_count; ++action) {
      auto &regret = regrets_[offset + action];
      if (regret > 0.0) {
        for (std::uint64_t step = static_cast<std::uint64_t>(last) + 1U; step <= iteration;
             ++step) {
          regret *= positive_discount_factors_[static_cast<std::size_t>(step)];
        }
      } else if (skipped > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        regret = 0.0;
      } else {
        regret = std::ldexp(regret, -static_cast<int>(skipped));
      }
      strategy_sums_[offset + action] *= strategy;
    }
  }
  last = static_cast<std::uint32_t>(iteration);
}

void Trainer::prepare_discount_factors(const std::uint64_t iteration) {
  while (positive_discount_factors_.size() <= iteration) {
    const double t = static_cast<double>(positive_discount_factors_.size());
    const double power = std::pow(t, config_.dcfr_alpha);
    const double positive = power / (power + 1.0);
    const double strategy = std::pow(t / (t + 1.0), config_.dcfr_gamma);
    positive_discount_factors_.push_back(positive);
    strategy_discount_prefix_.push_back(strategy_discount_prefix_.back() * strategy);
  }
}

void Trainer::materialize_all_discounts() {
  if (!config_.lazy_discount)
    return;
  executor_->run(game_->nodes().size(), [&](const std::size_t node_index, const unsigned) {
    const auto &node = game_->nodes()[node_index];
    if (node.kind != NodeKind::Decision)
      return;
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    for (std::uint32_t row = 0; row < rows; ++row)
      materialize_row(node.id, row, discount_target_);
  });
}

void Trainer::discount_state(const std::uint64_t iteration) {
  const double t = static_cast<double>(iteration);
  const double positive_power = std::pow(t, config_.dcfr_alpha);
  const double negative_power = std::pow(t, config_.dcfr_beta);
  const double positive_discount = positive_power / (positive_power + 1.0);
  const double negative_discount = negative_power / (negative_power + 1.0);
  const double strategy_discount = std::pow(t / (t + 1.0), config_.dcfr_gamma);
  for (auto &regret : regrets_) {
    regret *= regret > 0.0 ? positive_discount : negative_discount;
  }
  for (auto &sum : strategy_sums_) {
    sum *= strategy_discount;
  }
}

const double *Trainer::policy_row(const std::uint32_t node, const std::uint16_t hand,
                                  const BoardContext &context) const noexcept {
  const auto &entry = game_->nodes()[node];
  const auto offset = layout_.offsets[node] +
                      static_cast<std::uint64_t>(context.row(entry.street, hand)) *
                          entry.action_count;
  return policy_.data() + offset;
}

std::uint64_t Trainer::cell_offset(const std::uint32_t node, const std::uint16_t hand,
                                   const BoardContext &context) const noexcept {
  const auto &entry = game_->nodes()[node];
  return layout_.offsets[node] +
         static_cast<std::uint64_t>(context.row(entry.street, hand)) * entry.action_count;
}

card_abstraction::BoardHistory Trainer::sample_history(card_abstraction::DeterministicRandom &random,
                                                      double &weight) const {
  if (!board_list_.empty()) {
    const auto quantile = random.uniform_unit();
    const auto found = std::lower_bound(board_cumulative_.begin(), board_cumulative_.end(), quantile);
    const auto index = static_cast<std::size_t>(
        std::min<std::ptrdiff_t>(found - board_cumulative_.begin(),
                                 static_cast<std::ptrdiff_t>(board_list_.size()) - 1));
    weight = 1.0;
    return board_list_[index];
  }
  weight = 1.0;
  return resources_.catalog->sample_physical_history(random);
}

Result<IterationTelemetry, TrainerError> Trainer::iterate() {
  using Outcome = Result<IterationTelemetry, TrainerError>;
  if (!usable_)
    return Outcome::failure(TrainerError::IntegrityFailure);
  const auto started = Clock::now();
  const auto iteration = iteration_ + 1U;
  discount_target_ = iteration - 1U;
  if (config_.lazy_discount)
    prepare_discount_factors(discount_target_);
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
  if (!sample_boards_) {
    batch.resize(board_list_.size());
    std::atomic<bool> failed{false};
    executor_->run(board_list_.size(), [&](const std::size_t index, const unsigned) {
      const auto prepared = prepare_board(board_list_[index], board_weights_[index], batch[index]);
      if (!prepared)
        failed.store(true, std::memory_order_relaxed);
    });
    if (failed.load(std::memory_order_relaxed))
      return Outcome::failure(TrainerError::BoardFailure);
    record_prepare_cpu(batch);
  } else {
    batch.resize(config_.batch_boards);
    std::vector<card_abstraction::BoardHistory> histories(batch.size());
    std::vector<double> weights(batch.size());
    for (std::size_t index = 0; index < batch.size(); ++index) {
      double weight = 1.0;
      histories[index] = sample_history(training_random_, weight);
      weights[index] = weight / config_.batch_boards;
    }
    std::atomic<bool> failed{false};
    executor_->run(batch.size(), [&](const std::size_t index, const unsigned) {
      const auto prepared = prepare_board(histories[index], weights[index], batch[index]);
      if (!prepared)
        failed.store(true, std::memory_order_relaxed);
    });
    if (failed.load(std::memory_order_relaxed))
      return Outcome::failure(TrainerError::BoardFailure);
    record_prepare_cpu(batch);

    // Reused for the independent second-player chance batch below.
    auto prepare_next_sample = [&]() -> bool {
      for (std::size_t index = 0; index < batch.size(); ++index) {
        double weight = 1.0;
        histories[index] = sample_history(training_random_, weight);
        weights[index] = weight / config_.batch_boards;
      }
      failed.store(false, std::memory_order_relaxed);
      executor_->run(batch.size(), [&](const std::size_t index, const unsigned) {
        const auto prepared = prepare_board(histories[index], weights[index], batch[index]);
        if (!prepared)
          failed.store(true, std::memory_order_relaxed);
      });
      return !failed.load(std::memory_order_relaxed);
    };

    telemetry.board_prepare_seconds = std::chrono::duration<double>(Clock::now() - phase).count();
    for (auto &workspace : workspaces_)
      workspace->nodes_visited = workspace->decision_nodes_visited =
          workspace->hero_decision_nodes = workspace->opponent_decision_nodes =
              workspace->chance_nodes_visited = workspace->fold_terminals_visited =
                  workspace->preflop_all_in_terminals_visited =
                      workspace->postflop_showdown_terminals_visited =
                          workspace->zero_reach_prunes = workspace->policy_rows_read =
                              workspace->regret_cells_written =
                                  workspace->strategy_cells_written = 0U;
    for (auto &workspace : workspaces_)
      workspace->sampled_hero_reach_seconds = workspace->sampled_hero_update_seconds =
          workspace->sampled_opponent_reach_seconds =
              workspace->sampled_opponent_accumulate_seconds =
                  workspace->sampled_fold_terminal_seconds =
                      workspace->sampled_preflop_all_in_seconds =
                          workspace->sampled_postflop_showdown_seconds = 0.0;
    std::size_t drawn_boards = batch.size();
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      std::optional<ActiveRows> active_rows;
      if (hero == 1U && config_.update_mode == UpdateMode::Alternating) {
        phase = Clock::now();
        if (!prepare_next_sample())
          return Outcome::failure(TrainerError::BoardFailure);
        record_prepare_cpu(batch);
        drawn_boards += batch.size();
        telemetry.board_prepare_seconds +=
            std::chrono::duration<double>(Clock::now() - phase).count();
      }
      if (config_.update_mode == UpdateMode::Alternating &&
          config_.reuse_discount_invariant_policy) {
        phase = Clock::now();
        active_rows.emplace(collect_active_rows(batch));
        materialize_active_rows(*active_rows, hero);
        telemetry.policy_refresh_seconds +=
            std::chrono::duration<double>(Clock::now() - phase).count();
      } else if (hero == 0U || config_.update_mode == UpdateMode::Alternating) {
        phase = Clock::now();
        refresh_policy(config_.batch_policy_refresh ? &batch : nullptr);
        telemetry.policy_refresh_seconds +=
            std::chrono::duration<double>(Clock::now() - phase).count();
      }
      phase = Clock::now();
      for (const auto &work : batch)
        pass(work, hero, iteration_weight, &telemetry);
      telemetry.traversal_seconds += std::chrono::duration<double>(Clock::now() - phase).count();
      if (config_.update_mode == UpdateMode::Alternating &&
          config_.reuse_discount_invariant_policy) {
        phase = Clock::now();
        refresh_policy(config_.batch_policy_refresh ? &batch : nullptr, hero,
                       config_.batch_policy_refresh && active_rows ? &*active_rows : nullptr);
        telemetry.policy_refresh_seconds +=
            std::chrono::duration<double>(Clock::now() - phase).count();
      }
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
      telemetry.preflop_all_in_terminals_visited +=
          workspace->preflop_all_in_terminals_visited;
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
      telemetry.sampled_preflop_all_in_seconds +=
          workspace->sampled_preflop_all_in_seconds;
      telemetry.sampled_postflop_showdown_seconds +=
          workspace->sampled_postflop_showdown_seconds;
    }
    telemetry.process_bytes = process_working_set_bytes();
    return Outcome::success(telemetry);
  }

  telemetry.board_prepare_seconds = std::chrono::duration<double>(Clock::now() - phase).count();
  for (auto &workspace : workspaces_) {
    workspace->nodes_visited = workspace->decision_nodes_visited =
        workspace->hero_decision_nodes = workspace->opponent_decision_nodes =
            workspace->chance_nodes_visited = workspace->fold_terminals_visited =
                workspace->preflop_all_in_terminals_visited =
                    workspace->postflop_showdown_terminals_visited =
                        workspace->zero_reach_prunes = workspace->policy_rows_read =
                            workspace->regret_cells_written =
                                workspace->strategy_cells_written = 0U;
    workspace->sampled_hero_reach_seconds = workspace->sampled_hero_update_seconds =
        workspace->sampled_opponent_reach_seconds =
            workspace->sampled_opponent_accumulate_seconds =
                workspace->sampled_fold_terminal_seconds =
                    workspace->sampled_preflop_all_in_seconds =
                        workspace->sampled_postflop_showdown_seconds = 0.0;
  }
  std::size_t drawn_boards = batch.size();
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    std::optional<ActiveRows> active_rows;
    if (hero == 1U && config_.update_mode == UpdateMode::Alternating) {
      phase = Clock::now();
      if (sample_boards_) {
        // The opponent's new policy depends on the first batch. Reusing that
        // batch here conditions chance on the policy being evaluated, biasing
        // the second player's counterfactual values. Draw from the unchanged
        // chance law. Snapshot the selected rows after preparing this independent
        // batch, before any update; exact traversals can reuse their list.
        for (auto &work : batch) {
          double weight = 1.0;
          const auto history = sample_history(training_random_, weight);
          const auto prepared = prepare_board(history, weight / config_.batch_boards, work);
          if (!prepared) {
            return Outcome::failure(prepared.error());
          }
        }
        drawn_boards += batch.size();
      }
      telemetry.board_prepare_seconds +=
          std::chrono::duration<double>(Clock::now() - phase).count();
    }
    if (config_.update_mode == UpdateMode::Alternating &&
        config_.reuse_discount_invariant_policy) {
      phase = Clock::now();
      active_rows.emplace(collect_active_rows(batch));
      materialize_active_rows(*active_rows, hero);
      telemetry.policy_refresh_seconds +=
          std::chrono::duration<double>(Clock::now() - phase).count();
    } else if (hero == 0U || config_.update_mode == UpdateMode::Alternating) {
      phase = Clock::now();
      refresh_policy(config_.batch_policy_refresh ? &batch : nullptr);
      telemetry.policy_refresh_seconds +=
          std::chrono::duration<double>(Clock::now() - phase).count();
    }
    phase = Clock::now();
    for (const auto &work : batch) {
      pass(work, hero, iteration_weight, &telemetry);
    }
    telemetry.traversal_seconds += std::chrono::duration<double>(Clock::now() - phase).count();
    if (config_.update_mode == UpdateMode::Alternating &&
        config_.reuse_discount_invariant_policy) {
      phase = Clock::now();
      refresh_policy(config_.batch_policy_refresh ? &batch : nullptr, hero,
                     config_.batch_policy_refresh && active_rows ? &*active_rows : nullptr);
      telemetry.policy_refresh_seconds +=
          std::chrono::duration<double>(Clock::now() - phase).count();
    }
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
    telemetry.preflop_all_in_terminals_visited +=
        workspace->preflop_all_in_terminals_visited;
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
    const auto probabilities =
        policy_row(node_id, static_cast<std::uint16_t>(hand), board.context);
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
      const double reach = hero_reach[hand];
      if (reach == 0.0) {
        for (std::uint8_t action = 0; action < actions; ++action) {
          level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
        }
        continue;
      }
      const double *probabilities = policy_.data() + cell;
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
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      const auto cell = level.cell_offsets[hand];
      const double *probabilities = policy_.data() + cell;
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
          regrets_[cell + action] +=
              regret_weight *
              (level.child_values[static_cast<std::size_t>(action) * live_hand_count + hand] - value);
        }
      }
      const double strategy_weight = workspace.strategy_weight[hand] * hero_reach[hand];
      if (!fixed_policy_evaluation_ && strategy_weight != 0.0) {
        strategy_cells += actions;
        for (std::uint8_t action = 0; action < actions; ++action) {
          strategy_sums_[cell + action] += strategy_weight * probabilities[action];
        }
      }
    }
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
    const auto probabilities =
        policy_row(node_id, static_cast<std::uint16_t>(hand), board.context);
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
    if (!board.has_all_in) {
      std::fill_n(values, live_hand_count, 0.0);
      return;
    }
    board.all_in.masses(board.context, reach, first, second, third);
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

BucketPolicy Trainer::average_policy() {
  std::vector<double> table(layout_.entries);
  fill_average_policy(table);
  return BucketPolicy(*game_, layout_, std::move(table));
}

void Trainer::fill_average_policy(std::vector<double> &table) {
  if (!usable_)
    throw std::logic_error("trainer state invalid after failed checkpoint load");
  materialize_all_discounts();
  table.resize(layout_.entries);
  for (const auto &node : game_->nodes()) {
    if (node.kind != NodeKind::Decision) {
      continue;
    }
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    const auto actions = node.action_count;
    const auto base = layout_.offsets[node.id];
    for (std::uint32_t row = 0; row < rows; ++row) {
      const auto offset = base + static_cast<std::uint64_t>(row) * actions;
      double total = 0.0;
      for (std::uint8_t action = 0; action < actions; ++action) {
        total += std::max(0.0, strategy_sums_[offset + action]);
      }
      if (total > 0.0) {
        for (std::uint8_t action = 0; action < actions; ++action) {
          table[offset + action] = std::max(0.0, strategy_sums_[offset + action]) / total;
        }
        continue;
      }
      double positive = 0.0;
      for (std::uint8_t action = 0; action < actions; ++action) {
        positive += std::max(0.0, regrets_[offset + action]);
      }
      for (std::uint8_t action = 0; action < actions; ++action) {
        table[offset + action] = positive > 0.0
                                     ? std::max(0.0, regrets_[offset + action]) / positive
                                     : 1.0 / actions;
      }
    }
  }
}

BucketPolicy Trainer::current_policy() {
  if (!usable_)
    throw std::logic_error("trainer state invalid after failed checkpoint load");
  materialize_all_discounts();
  BucketPolicy policy(*game_, layout_);
  auto &table = policy.table();
  for (const auto &node : game_->nodes()) {
    if (node.kind != NodeKind::Decision) {
      continue;
    }
    const auto rows = StateLayout::rows_for(node.street, config_.flop_capacity,
                                            config_.turn_capacity, config_.river_capacity);
    const auto actions = node.action_count;
    const auto base = layout_.offsets[node.id];
    for (std::uint32_t row = 0; row < rows; ++row) {
      const auto offset = base + static_cast<std::uint64_t>(row) * actions;
      double positive = 0.0;
      for (std::uint8_t action = 0; action < actions; ++action) {
        positive += std::max(0.0, regrets_[offset + action]);
      }
      for (std::uint8_t action = 0; action < actions; ++action) {
        table[offset + action] = positive > 0.0
                                     ? std::max(0.0, regrets_[offset + action]) / positive
                                     : 1.0 / actions;
      }
    }
  }
  return policy;
}

BucketPolicy Trainer::take_average_policy() {
  fill_average_policy(policy_);
  return BucketPolicy(*game_, layout_, std::move(policy_));
}

BucketPolicy Trainer::take_current_policy() {
  if (!usable_)
    throw std::logic_error("trainer state invalid after failed checkpoint load");
  refresh_policy();
  return BucketPolicy(*game_, layout_, std::move(policy_));
}

void Trainer::restore_policy_buffer(BucketPolicy &&policy) {
  if (policy.layout().entries != layout_.entries || policy.table().size() != layout_.entries)
    throw std::invalid_argument("policy buffer layout mismatch");
  policy_ = std::move(policy.table());
  refresh_policy();
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
  auto average = take_average_policy();
  const auto report = evaluate_best_response(*game_, average, resources, groups, options);
  restore_policy_buffer(std::move(average));
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
  const std::string_view regrets(reinterpret_cast<const char *>(regrets_.data()),
                                 regrets_.size() * sizeof(double));
  const std::string_view sums(reinterpret_cast<const char *>(strategy_sums_.data()),
                              strategy_sums_.size() * sizeof(double));
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
  append_little(buffer, regrets_.size());

  const auto temporary = std::filesystem::path(path.string() + ".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Outcome::failure(TrainerError::IoFailure);
    }
    const std::array<std::span<const double>, 2> arrays{regrets_, strategy_sums_};
    if (!stream_io::write(output, buffer, arrays)) {
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
    if (!body.u64(entries) || entries != regrets_.size() || body.remaining() != entries * 16ULL)
      return TrainerError::IntegrityFailure;
    return std::nullopt;
  };
  stream_io::Reader validation(input);
  const auto header_status = read_header(validation);
  if (header_status)
    return Outcome::failure(*header_status);
  if (!validation.scan_doubles(regrets_.size()) ||
      !validation.scan_doubles(strategy_sums_.size(), true) || !validation.finish())
    return Outcome::failure(TrainerError::IntegrityFailure);
  // Validate the complete file before changing existing state. The second
  // pass uses the already allocated arrays. A changed file or an I/O failure
  // during this commit poisons the trainer until another successful load.
  stream_io::Reader commit(input);
  if (read_header(commit))
    return Outcome::failure(TrainerError::IntegrityFailure);
  usable_ = false;
  if (!commit.doubles(regrets_) || !commit.doubles(strategy_sums_, true) || !commit.finish())
    return Outcome::failure(TrainerError::IntegrityFailure);
  iteration_ = iteration;
  boards_processed_ = boards_processed;
  training_random_.restore(training_state);
  evaluation_random_.restore(evaluation_state);
  discount_target_ = iteration_ > 0U ? iteration_ - 1U : 0U;
  if (config_.lazy_discount) {
    if (discount_target_ > std::numeric_limits<std::uint32_t>::max())
      return Outcome::failure(TrainerError::UnsupportedVersion);
    std::fill(discount_iterations_.begin(), discount_iterations_.end(),
              static_cast<std::uint32_t>(discount_target_));
    prepare_discount_factors(discount_target_);
  }
  usable_ = true;
  refresh_policy();
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
          const double value = *std::max_element(evaluator->regrets_.begin() + offset,
                                                 evaluator->regrets_.begin() + offset +
                                                     node.action_count);
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
          evaluator->regrets_[parent_offset] += value;
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

} // namespace gtosd::preflop_blueprint
