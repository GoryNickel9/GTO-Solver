#include "gtosd/preflop_blueprint/trainer.hpp"

#include "hashing.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numeric>
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
constexpr std::uint32_t checkpoint_version = 1U;

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

template <typename Function>
void run_parallel(const unsigned threads, const std::size_t count, Function &&function) {
  const auto workers = static_cast<std::size_t>(std::max(1U, threads));
  if (workers <= 1U || count <= 1U) {
    for (std::size_t index = 0; index < count; ++index) {
      function(index, 0U);
    }
    return;
  }
  std::atomic<std::size_t> next{0U};
  const auto worker = [&](const unsigned thread) {
    for (std::size_t index = next.fetch_add(1U); index < count; index = next.fetch_add(1U)) {
      function(index, thread);
    }
  };
  std::vector<std::thread> pool;
  const auto spawned = std::min(workers, count) - 1U;
  pool.reserve(spawned);
  for (unsigned thread = 1; thread <= spawned; ++thread) {
    pool.emplace_back(worker, thread);
  }
  worker(0U);
  for (auto &thread : pool) {
    thread.join();
  }
}

void append_little(std::string &buffer, const std::uint64_t value) {
  for (unsigned shift = 0; shift < 64U; shift += 8U) {
    buffer.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

void append_little32(std::string &buffer, const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32U; shift += 8U) {
    buffer.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

void append_doubles(std::string &buffer, const std::vector<double> &values) {
  const auto offset = buffer.size();
  buffer.resize(offset + values.size() * sizeof(double));
  if (!values.empty()) {
    std::memcpy(buffer.data() + offset, values.data(), values.size() * sizeof(double));
  }
}

class Reader {
public:
  explicit Reader(const std::string &data) : data_(data) {}
  bool read_little(std::uint64_t &value) {
    if (position_ + 8U > data_.size()) {
      return false;
    }
    value = 0U;
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
      value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[position_++])) << shift;
    }
    return true;
  }
  bool read_little32(std::uint32_t &value) {
    if (position_ + 4U > data_.size()) {
      return false;
    }
    value = 0U;
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[position_++])) << shift;
    }
    return true;
  }
  bool read_doubles(std::vector<double> &values, const std::size_t count) {
    if (position_ + count * sizeof(double) > data_.size()) {
      return false;
    }
    values.resize(count);
    if (count > 0U) {
      std::memcpy(values.data(), data_.data() + position_, count * sizeof(double));
    }
    position_ += count * sizeof(double);
    return true;
  }
  bool read_string(std::string &value) {
    std::uint32_t length = 0U;
    if (!read_little32(length) || position_ + length > data_.size()) {
      return false;
    }
    value.assign(data_.data() + position_, length);
    position_ += length;
    return true;
  }
  [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
  const std::string &data_;
  std::size_t position_{0U};
};

} // namespace

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
  };
  std::vector<Level> levels;
  std::vector<double> regret_weight;
  std::vector<double> strategy_weight;
  std::uint64_t nodes_visited{0U};

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
    : game_(&game), resources_(resources), config_(config), training_random_(config.training_seed),
      evaluation_random_(config.evaluation_seed) {}

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
                                               const HandSubsets *subsets) {
  using Outcome = Result<bool, TrainerError>;
  const auto &stats = game_->stats();
  const bool postflop_decisions = stats.decision_nodes > stats.preflop_decisions;
  if (postflop_decisions) {
    if (resources_.catalog == nullptr || resources_.flop == nullptr || resources_.turn == nullptr ||
        resources_.river == nullptr) {
      return Outcome::failure(TrainerError::MissingResource);
    }
    if (resources_.flop->capacity() != config_.flop_capacity ||
        resources_.turn->capacity() != config_.turn_capacity ||
        resources_.river->capacity() != config_.river_capacity) {
      return Outcome::failure(TrainerError::InvalidConfiguration);
    }
  }
  if (stats.preflop_all_in_runouts > 0U && resources_.all_in == nullptr) {
    return Outcome::failure(TrainerError::MissingResource);
  }

  layout_ = layout_state(*game_, config_.flop_capacity, config_.turn_capacity,
                         config_.river_capacity);
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
  strategy_sums_.assign(layout_.entries, 0.0);
  policy_.assign(layout_.entries, 0.0);
  refresh_policy();
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

  std::string identity = "gtosd.preflop_blueprint_trainer.v1|" + game_->fingerprint() + "|" +
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
              (sample_boards_ ? "sampled" : "exact") + "|" +
              std::to_string(board_list_.size()) + "|" + (subsets_ ? "subsets" : "all_hands");
  identity_ = "fnv1a64:" + detail::hex64_text(detail::fnv1a_text(identity));
  return Outcome::success(true);
}

Result<bool, TrainerError> Trainer::prepare_board(const card_abstraction::BoardHistory &history,
                                                  const double weight, BoardWork &work) const {
  using Outcome = Result<bool, TrainerError>;
  AbstractionTables tables;
  tables.catalog = resources_.catalog;
  tables.flop = resources_.flop;
  tables.turn = resources_.turn;
  tables.river = resources_.river;
  const bool with_tables = resources_.catalog != nullptr && resources_.flop != nullptr &&
                           resources_.turn != nullptr && resources_.river != nullptr;
  auto context = BoardContext::build(history, *resources_.ranks, with_tables ? &tables : nullptr);
  if (!context) {
    return Outcome::failure(TrainerError::BoardFailure);
  }
  work.context = std::move(context.value());
  work.weight = weight;
  work.has_all_in = false;
  if (game_->stats().preflop_all_in_runouts > 0U && resources_.all_in != nullptr) {
    auto cache = AllInEquityCache::build(work.context, *resources_.all_in);
    if (!cache) {
      return Outcome::failure(TrainerError::BoardFailure);
    }
    work.all_in = std::move(cache.value());
    work.has_all_in = true;
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
  return Outcome::success(true);
}

void Trainer::refresh_policy() {
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
  }
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
  return policy_.data() + layout_.offsets[node] +
         static_cast<std::uint64_t>(context.row(entry.street, hand)) * entry.action_count;
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
  const auto started = Clock::now();
  const auto iteration = iteration_ + 1U;
  if (config_.scheme == WeightingScheme::Dcfr && iteration > 1U) {
    discount_state(iteration - 1U);
  }
  refresh_policy();
  const double iteration_weight =
      config_.scheme == WeightingScheme::Linear ? static_cast<double>(iteration) : 1.0;

  std::vector<BoardWork> batch;
  if (!sample_boards_) {
    batch.resize(board_list_.size());
    for (std::size_t index = 0; index < board_list_.size(); ++index) {
      const auto prepared = prepare_board(board_list_[index], board_weights_[index], batch[index]);
      if (!prepared) {
        return Outcome::failure(prepared.error());
      }
    }
  } else {
    batch.resize(config_.batch_boards);
    for (auto &work : batch) {
      double weight = 1.0;
      const auto history = sample_history(training_random_, weight);
      const auto prepared = prepare_board(history, weight / config_.batch_boards, work);
      if (!prepared) {
        return Outcome::failure(prepared.error());
      }
    }
  }

  for (auto &workspace : workspaces_) {
    workspace->nodes_visited = 0U;
  }
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    if (hero == 1U && config_.update_mode == UpdateMode::Alternating) {
      refresh_policy();
    }
    for (const auto &work : batch) {
      pass(work, hero, iteration_weight);
    }
  }
  iteration_ = iteration;
  boards_processed_ += batch.size();

  IterationTelemetry telemetry;
  telemetry.iteration = iteration_;
  telemetry.boards = static_cast<std::uint32_t>(batch.size());
  telemetry.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  telemetry.seconds_per_board = telemetry.seconds / static_cast<double>(std::max<std::size_t>(1U, batch.size()));
  for (const auto &workspace : workspaces_) {
    telemetry.nodes_visited += workspace->nodes_visited;
  }
  telemetry.process_bytes = process_working_set_bytes();
  return Outcome::success(telemetry);
}

void Trainer::pass(const BoardWork &board, const std::uint8_t hero, const double iteration_weight) {
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

  const double *hero_reach = board.initial_reach[hero].data();
  const double *opponent_reach = board.initial_reach[opponent].data();
  top_down_reach(game_->root(), hero_reach, opponent_reach, hero, board, primary, 0U);
  run_parallel(config_.threads, units_.size(), [&](const std::size_t index, const unsigned thread) {
    auto &unit = units_[index];
    const auto depth = game_->nodes()[unit.root].depth;
    traverse(unit.root, depth, unit.hero_reach.data(), unit.opponent_reach.data(),
             unit.values.data(), hero, board, *workspaces_[thread], false);
  });
  traverse(game_->root(), 0U, hero_reach, opponent_reach, primary.levels[0].scratch.data(), hero,
           board, primary, true);
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
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double reach = acting[hand];
    if (reach == 0.0) {
      for (std::uint8_t action = 0; action < actions; ++action) {
        level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
      }
      continue;
    }
    const double *probabilities =
        policy_row(node_id, static_cast<std::uint16_t>(hand), board.context);
    for (std::uint8_t action = 0; action < actions; ++action) {
      level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] =
          reach * probabilities[action];
    }
  }
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
    std::fill_n(values, live_hand_count, 0.0);
    return;
  }
  switch (node.kind) {
  case NodeKind::TerminalFold:
  case NodeKind::TerminalShowdown:
    if (opponent_zero) {
      std::fill_n(values, live_hand_count, 0.0);
    } else {
      terminal(node, opponent_reach, values, workspace.levels[depth].scratch.data(), hero, board);
    }
    return;
  case NodeKind::Chance:
    traverse(game_->edges_of(node_id)[0].child, depth + 1U, hero_reach, opponent_reach, values,
             hero, board, workspace, top_phase);
    return;
  case NodeKind::Decision:
    break;
  }

  auto &level = workspace.levels[depth];
  const auto edges = game_->edges_of(node_id);
  const auto actions = node.action_count;
  if (node.actor == hero) {
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      const double reach = hero_reach[hand];
      if (reach == 0.0) {
        for (std::uint8_t action = 0; action < actions; ++action) {
          level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
        }
        continue;
      }
      const double *probabilities =
          policy_row(node_id, static_cast<std::uint16_t>(hand), board.context);
      for (std::uint8_t action = 0; action < actions; ++action) {
        level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] =
            reach * probabilities[action];
      }
    }
    for (std::uint8_t action = 0; action < actions; ++action) {
      traverse(edges[action].child, depth + 1U,
               level.child_reach.data() + static_cast<std::size_t>(action) * live_hand_count,
               opponent_reach,
               level.child_values.data() + static_cast<std::size_t>(action) * live_hand_count, hero,
               board, workspace, top_phase);
    }
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      const double *probabilities =
          policy_row(node_id, static_cast<std::uint16_t>(hand), board.context);
      double value = 0.0;
      for (std::uint8_t action = 0; action < actions; ++action) {
        value += probabilities[action] *
                 level.child_values[static_cast<std::size_t>(action) * live_hand_count + hand];
      }
      values[hand] = value;
      const auto cell = cell_offset(node_id, static_cast<std::uint16_t>(hand), board.context);
      const double regret_weight = workspace.regret_weight[hand];
      if (!opponent_zero && regret_weight != 0.0) {
        for (std::uint8_t action = 0; action < actions; ++action) {
          regrets_[cell + action] +=
              regret_weight *
              (level.child_values[static_cast<std::size_t>(action) * live_hand_count + hand] - value);
        }
      }
      const double strategy_weight = workspace.strategy_weight[hand] * hero_reach[hand];
      if (strategy_weight != 0.0) {
        for (std::uint8_t action = 0; action < actions; ++action) {
          strategy_sums_[cell + action] += strategy_weight * probabilities[action];
        }
      }
    }
    return;
  }

  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double reach = opponent_reach[hand];
    if (reach == 0.0) {
      for (std::uint8_t action = 0; action < actions; ++action) {
        level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
      }
      continue;
    }
    const double *probabilities =
        policy_row(node_id, static_cast<std::uint16_t>(hand), board.context);
    for (std::uint8_t action = 0; action < actions; ++action) {
      level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] =
          reach * probabilities[action];
    }
  }
  std::fill_n(values, live_hand_count, 0.0);
  for (std::uint8_t action = 0; action < actions; ++action) {
    traverse(edges[action].child, depth + 1U, hero_reach,
             level.child_reach.data() + static_cast<std::size_t>(action) * live_hand_count,
             level.child_values.data(), hero, board, workspace, top_phase);
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      values[hand] += level.child_values[hand];
    }
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

BucketPolicy Trainer::average_policy() const {
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
  return policy;
}

BucketPolicy Trainer::current_policy() const {
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

std::array<double, 2> Trainer::evaluate_board(const BoardWork &board, const BucketPolicy &average,
                                              std::array<double, 2> &best_response,
                                              Workspace &) const {
  ValueTraversal traversal(*game_, board.context, kernel_,
                           board.has_all_in ? &board.all_in : nullptr);
  std::array<double, 2> ev{};
  std::array<double, live_hand_count> values{};
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    const auto opponent = static_cast<std::uint8_t>(1U - hero);
    const ConstHandSpan reach(board.initial_reach[opponent].data(), live_hand_count);
    static_cast<void>(traversal.evaluate(average, hero, reach, values));
    double policy_value = 0.0;
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      policy_value +=
          board.hand_probability[hero][hand] * board.pair_probability[hero][hand] * values[hand];
    }
    TraversalOptions options;
    options.best_response = true;
    static_cast<void>(traversal.evaluate(average, hero, reach, values, options));
    double response_value = 0.0;
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      response_value +=
          board.hand_probability[hero][hand] * board.pair_probability[hero][hand] * values[hand];
    }
    ev[hero] = policy_value;
    best_response[hero] = response_value;
  }
  return ev;
}

Result<ExploitabilityEstimate, TrainerError>
Trainer::estimate_exploitability(const std::uint32_t boards, const bool exact_on_list) {
  using Outcome = Result<ExploitabilityEstimate, TrainerError>;
  const auto started = Clock::now();
  const auto average = average_policy();
  ExploitabilityEstimate estimate;
  const bool exact = !board_list_.empty() && (!sample_boards_ || exact_on_list);
  const auto count = exact ? board_list_.size() : static_cast<std::size_t>(boards);
  if (count == 0U) {
    return Outcome::failure(TrainerError::InvalidConfiguration);
  }
  std::vector<card_abstraction::BoardHistory> histories(count);
  std::vector<double> weights(count, 1.0 / static_cast<double>(count));
  if (exact) {
    histories = board_list_;
    weights = board_weights_;
  } else {
    for (auto &history : histories) {
      double weight = 1.0;
      history = sample_history(evaluation_random_, weight);
    }
  }
  std::vector<std::array<double, 2>> evs(count);
  std::vector<std::array<double, 2>> responses(count);
  std::atomic<bool> failed{false};
  run_parallel(config_.threads, count, [&](const std::size_t index, const unsigned thread) {
    BoardWork work;
    if (!prepare_board(histories[index], weights[index], work)) {
      failed.store(true);
      return;
    }
    evs[index] = evaluate_board(work, average, responses[index], *workspaces_[thread]);
  });
  if (failed.load()) {
    return Outcome::failure(TrainerError::BoardFailure);
  }
  estimate.boards = static_cast<std::uint32_t>(count);
  estimate.exact = exact;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    double ev_mean = 0.0;
    double response_mean = 0.0;
    double gain_mean = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
      ev_mean += weights[index] * evs[index][player];
      response_mean += weights[index] * responses[index][player];
      gain_mean += weights[index] * (responses[index][player] - evs[index][player]);
    }
    double variance = 0.0;
    if (!exact && count > 1U) {
      for (std::size_t index = 0; index < count; ++index) {
        const double gain = responses[index][player] - evs[index][player];
        variance += (gain - gain_mean) * (gain - gain_mean);
      }
      variance /= static_cast<double>(count - 1U);
    }
    estimate.ev[player] = ev_mean;
    estimate.best_response[player] = response_mean;
    estimate.gain[player] = gain_mean;
    estimate.gain_standard_error[player] =
        exact ? 0.0 : std::sqrt(variance / static_cast<double>(count));
  }
  const auto worst = estimate.gain[0] >= estimate.gain[1] ? 0U : 1U;
  estimate.max_gain = estimate.gain[worst];
  estimate.max_gain_half_width = 1.96 * estimate.gain_standard_error[worst];
  estimate.nashconv = estimate.gain[0] + estimate.gain[1];
  estimate.normalized_dev = initial_pot_antes_ > 0.0 ? estimate.max_gain / initial_pot_antes_ : 0.0;
  estimate.normalized_stack = stack_antes_ > 0.0 ? estimate.max_gain / stack_antes_ : 0.0;
  estimate.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  return Outcome::success(estimate);
}

std::string Trainer::state_fingerprint() const {
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

Result<bool, TrainerError> Trainer::save_checkpoint(const std::filesystem::path &path) const {
  using Outcome = Result<bool, TrainerError>;
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
  append_doubles(buffer, regrets_);
  append_doubles(buffer, strategy_sums_);
  append_little(buffer, detail::fnv1a_text(buffer));

  const auto temporary = std::filesystem::path(path.string() + ".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Outcome::failure(TrainerError::IoFailure);
    }
    output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    output.flush();
    if (!output) {
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
  std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (data.size() < checkpoint_magic.size() + 12U ||
      std::memcmp(data.data(), checkpoint_magic.data(), checkpoint_magic.size()) != 0) {
    return Outcome::failure(TrainerError::IntegrityFailure);
  }
  const auto payload = data.size() - 8U;
  const std::string checksum_bytes = data.substr(payload);
  Reader checksum_reader(checksum_bytes);
  std::uint64_t stored_checksum = 0U;
  if (!checksum_reader.read_little(stored_checksum) ||
      detail::fnv1a_text(std::string_view(data.data(), payload)) != stored_checksum) {
    return Outcome::failure(TrainerError::IntegrityFailure);
  }
  const std::string body_bytes =
      data.substr(checkpoint_magic.size(), payload - checkpoint_magic.size());
  Reader body(body_bytes);
  std::uint32_t version = 0U;
  std::string identity;
  std::uint64_t iteration = 0U;
  std::uint64_t boards_processed = 0U;
  card_abstraction::DeterministicRandom::State training_state{};
  card_abstraction::DeterministicRandom::State evaluation_state{};
  std::uint64_t entries = 0U;
  std::vector<double> regrets;
  std::vector<double> sums;
  if (!body.read_little32(version) || version != checkpoint_version) {
    return Outcome::failure(TrainerError::UnsupportedVersion);
  }
  if (!body.read_string(identity) || !body.read_little(iteration) ||
      !body.read_little(boards_processed)) {
    return Outcome::failure(TrainerError::IntegrityFailure);
  }
  for (auto &word : training_state) {
    if (!body.read_little(word)) {
      return Outcome::failure(TrainerError::IntegrityFailure);
    }
  }
  for (auto &word : evaluation_state) {
    if (!body.read_little(word)) {
      return Outcome::failure(TrainerError::IntegrityFailure);
    }
  }
  if (!body.read_little(entries) || entries != regrets_.size() ||
      !body.read_doubles(regrets, static_cast<std::size_t>(entries)) ||
      !body.read_doubles(sums, static_cast<std::size_t>(entries))) {
    return Outcome::failure(TrainerError::IntegrityFailure);
  }
  if (identity != identity_) {
    return Outcome::failure(TrainerError::IntegrityFailure);
  }
  iteration_ = iteration;
  boards_processed_ = boards_processed;
  training_random_.restore(training_state);
  evaluation_random_.restore(evaluation_state);
  regrets_ = std::move(regrets);
  strategy_sums_ = std::move(sums);
  refresh_policy();
  return Outcome::success(true);
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
