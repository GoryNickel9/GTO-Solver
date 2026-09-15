#include "gtosd/preflop/hu_preflop.hpp"

#include "gtosd/core/external_sampling.hpp"

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/equity/seven_card_table.hpp"
#include "gtosd/equity/showdown.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace gtosd {
namespace {

constexpr std::size_t maximum_actions = hu_preflop_sampled_postflop_maximum_actions;
constexpr std::uint16_t unset_bucket = hu_preflop_sampled_postflop_unset_bucket;
constexpr std::uint64_t postflop_history_seed = 0x5052'4546'4C4F'5001ULL;
constexpr std::uint64_t policy_fingerprint_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t policy_fingerprint_prime = 1'099'511'628'211ULL;
constexpr std::size_t maximum_root_decision_trace_hand_classes = 16U;
constexpr std::uint32_t maximum_root_decision_trace_deals_per_class = 10'000U;
constexpr std::uint64_t maximum_root_decision_trace_class_deals = 20'000U;

struct Deal {
  std::array<std::array<CardId, 2>, 2> holes{};
  std::array<CardId, 5> board{};
  std::array<std::array<std::uint16_t, 3>, 2> buckets{{
      {unset_bucket, unset_bucket, unset_bucket},
      {unset_bucket, unset_bucket, unset_bucket},
  }};
  std::int8_t winner_mask{-1};
};

struct InformationKey {
  std::uint64_t public_history{0};
  std::uint64_t physical_cards{std::numeric_limits<std::uint64_t>::max()};
  std::array<std::uint16_t, 3> bucket_history{unset_bucket, unset_bucket, unset_bucket};
  HandClassId preflop_class{0};
  std::uint8_t player{0};
  Street street{Street::Preflop};

  friend bool operator==(const InformationKey &, const InformationKey &) = default;
};

struct InformationKeyHash {
  std::size_t operator()(const InformationKey &key) const noexcept {
    std::uint64_t value = key.public_history ^ key.physical_cards ^
                          (static_cast<std::uint64_t>(key.preflop_class) << 48U) ^
                          (static_cast<std::uint64_t>(key.player) << 56U) ^
                          (static_cast<std::uint64_t>(key.street) << 58U);
    for (const auto bucket : key.bucket_history) {
      value ^= static_cast<std::uint64_t>(bucket) + 0x9E37'79B9'7F4A'7C15ULL + (value << 6U) +
               (value >> 2U);
    }
    value ^= value >> 30U;
    value *= 0xBF58'476D'1CE4'E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D0'49BB'1331'11EBULL;
    value ^= value >> 31U;
    return static_cast<std::size_t>(value);
  }
};

struct InformationState {
  std::array<double, maximum_actions> regrets{};
  std::array<double, maximum_actions> strategy_sum{};
  std::uint64_t last_iteration{0};
  std::uint8_t action_count{0};
};

class DcfrTable {
public:
  explicit DcfrTable(
      const std::uint64_t maximum_iteration,
      const HuPreflopSamplingAlgorithm algorithm = HuPreflopSamplingAlgorithm::DiscountedMccfr1503,
      const std::uint64_t maximum_states = std::numeric_limits<std::uint64_t>::max())
      : maximum_states_(maximum_states), algorithm_(algorithm) {
    if (algorithm_ == HuPreflopSamplingAlgorithm::DiscountedMccfr1503) {
      positive_log_prefix_.resize(static_cast<std::size_t>(maximum_iteration + 1U), 0.0);
      for (std::uint64_t iteration = 1U; iteration <= maximum_iteration; ++iteration) {
        const auto powered = std::pow(static_cast<double>(iteration), 1.5);
        positive_log_prefix_[static_cast<std::size_t>(iteration)] =
            positive_log_prefix_[static_cast<std::size_t>(iteration - 1U)] +
            std::log(powered / (powered + 1.0));
      }
    }
  }

  InformationState &touch(const InformationKey &key, const std::size_t action_count,
                          const std::uint64_t iteration) {
    const auto existing = key_to_index_.find(key);
    if (existing == key_to_index_.end() && size_ >= maximum_states_) {
      valid_ = false;
      memory_exhausted_ = true;
      overflow_state_ = {};
      overflow_state_.action_count = static_cast<std::uint8_t>(action_count);
      overflow_state_.last_iteration = iteration;
      return overflow_state_;
    }
    if (existing == key_to_index_.end()) {
      return append_state(key, action_count, iteration);
    }
    auto &state = state_at(existing->second);
    if (state.action_count != action_count || iteration < state.last_iteration ||
        (algorithm_ == HuPreflopSamplingAlgorithm::DiscountedMccfr1503 &&
         iteration >= positive_log_prefix_.size())) {
      valid_ = false;
      return state;
    }
    if (iteration == state.last_iteration) {
      return state;
    }
    if (algorithm_ != HuPreflopSamplingAlgorithm::DiscountedMccfr1503) {
      state.last_iteration = iteration;
      return state;
    }
    const auto discount = dcfr_1503_lazy_discount(
        state.last_iteration, iteration,
        positive_log_prefix_[static_cast<std::size_t>(iteration)] -
            positive_log_prefix_[static_cast<std::size_t>(state.last_iteration)]);
    for (std::size_t action = 0; action < action_count; ++action) {
      state.regrets[action] *=
          state.regrets[action] >= 0.0 ? discount.positive_regret : discount.negative_regret;
      state.strategy_sum[action] *= discount.average_strategy;
    }
    state.last_iteration = iteration;
    return state;
  }

  [[nodiscard]] std::array<double, maximum_actions>
  current_strategy(const InformationState &state) const {
    std::array<double, maximum_actions> strategy{};
    double positive_total = 0.0;
    for (std::size_t action = 0; action < state.action_count; ++action) {
      strategy[action] = std::max(0.0, state.regrets[action]);
      positive_total += strategy[action];
    }
    if (positive_total > 0.0) {
      for (std::size_t action = 0; action < state.action_count; ++action) {
        strategy[action] /= positive_total;
      }
    } else {
      const auto uniform = 1.0 / static_cast<double>(state.action_count);
      std::fill_n(strategy.begin(), state.action_count, uniform);
    }
    return strategy;
  }

  [[nodiscard]] Result<std::array<double, maximum_actions>, HuPreflopError>
  frozen_current_strategy(const InformationKey &key, const std::size_t action_count) const {
    const auto found = key_to_index_.find(key);
    if (found == key_to_index_.end()) {
      std::array<double, maximum_actions> strategy{};
      std::fill_n(strategy.begin(), action_count, 1.0 / static_cast<double>(action_count));
      return Result<std::array<double, maximum_actions>, HuPreflopError>::success(strategy);
    }
    const auto &state = state_at(found->second);
    return state.action_count == action_count
               ? Result<std::array<double, maximum_actions>, HuPreflopError>::success(
                     current_strategy(state))
               : Result<std::array<double, maximum_actions>, HuPreflopError>::failure(
                     HuPreflopError::IntegrityFailure);
  }

  [[nodiscard]] std::array<double, maximum_actions>
  average_strategy(const InformationKey &key, const std::size_t action_count) const {
    std::array<double, maximum_actions> strategy{};
    ++average_policy_queries_;
    const auto found = key_to_index_.find(key);
    if (found == key_to_index_.end() || state_at(found->second).action_count != action_count) {
      ++missing_average_policy_queries_;
      const auto uniform = 1.0 / static_cast<double>(action_count);
      std::fill_n(strategy.begin(), action_count, uniform);
      return strategy;
    }
    const auto &state = state_at(found->second);
    const auto total = std::accumulate(state.strategy_sum.begin(),
                                       state.strategy_sum.begin() + state.action_count, 0.0);
    if (total > 0.0 && std::isfinite(total)) {
      for (std::size_t action = 0; action < action_count; ++action) {
        strategy[action] = state.strategy_sum[action] / total;
      }
      return strategy;
    }
    return current_strategy(state);
  }

  [[nodiscard]] const InformationState *find(const InformationKey &key,
                                             const std::size_t action_count) const noexcept {
    const auto found = key_to_index_.find(key);
    if (found == key_to_index_.end()) {
      return nullptr;
    }
    const auto &state = state_at(found->second);
    return state.action_count == action_count ? &state : nullptr;
  }

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] HuPreflopError error() const noexcept {
    return memory_exhausted_ ? HuPreflopError::MemoryFailure : HuPreflopError::NumericalFailure;
  }
  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  [[nodiscard]] std::uint64_t average_policy_queries() const noexcept {
    return average_policy_queries_;
  }
  [[nodiscard]] std::uint64_t missing_average_policy_queries() const noexcept {
    return missing_average_policy_queries_;
  }

  template <typename Function> void for_each_state(Function &&function) const {
    for (const auto &[key, index] : key_to_index_) {
      function(key, state_at(index));
    }
  }

private:
  static constexpr std::uint64_t state_chunk_capacity = 1'024U;

  struct StateChunk {
    std::unique_ptr<InformationState[]> states;
    std::size_t capacity{0U};
    std::size_t used{0U};
  };

  InformationState &state_at(const std::uint64_t index) {
    const auto chunk = static_cast<std::size_t>(index / state_chunk_capacity);
    const auto offset = static_cast<std::size_t>(index % state_chunk_capacity);
    return chunks_[chunk].states[offset];
  }

  const InformationState &state_at(const std::uint64_t index) const {
    const auto chunk = static_cast<std::size_t>(index / state_chunk_capacity);
    const auto offset = static_cast<std::size_t>(index % state_chunk_capacity);
    return chunks_[chunk].states[offset];
  }

  InformationState &append_state(const InformationKey &key, const std::size_t action_count,
                                 const std::uint64_t iteration) {
    try {
      if (chunks_.empty() || chunks_.back().used == chunks_.back().capacity) {
        const auto capacity =
            static_cast<std::size_t>(std::min(state_chunk_capacity, maximum_states_ - size_));
        StateChunk chunk;
        chunk.states = std::make_unique<InformationState[]>(capacity);
        chunk.capacity = capacity;
        chunks_.push_back(std::move(chunk));
      }
      auto &chunk = chunks_.back();
      const auto offset = chunk.used;
      chunk.states[offset] = {};
      chunk.states[offset].action_count = static_cast<std::uint8_t>(action_count);
      chunk.states[offset].last_iteration = iteration;
      key_to_index_.emplace(key, size_);
      ++chunk.used;
      ++size_;
      return chunk.states[offset];
    } catch (const std::bad_alloc &) {
      valid_ = false;
      memory_exhausted_ = true;
      overflow_state_ = {};
      overflow_state_.action_count = static_cast<std::uint8_t>(action_count);
      overflow_state_.last_iteration = iteration;
      return overflow_state_;
    }
  }

  std::unordered_map<InformationKey, std::uint64_t, InformationKeyHash> key_to_index_;
  std::vector<StateChunk> chunks_;
  std::vector<double> positive_log_prefix_;
  std::uint64_t maximum_states_{std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t size_{0U};
  HuPreflopSamplingAlgorithm algorithm_{HuPreflopSamplingAlgorithm::DiscountedMccfr1503};
  InformationState overflow_state_{};
  bool valid_{true};
  bool memory_exhausted_{false};
  mutable std::uint64_t average_policy_queries_{0U};
  mutable std::uint64_t missing_average_policy_queries_{0U};
};

struct OpponentValueBaselineState {
  std::array<double, maximum_actions> means{};
  std::array<std::uint64_t, maximum_actions> sample_counts{};
  std::uint8_t action_count{0U};
};

class OpponentValueBaselineTable {
public:
  explicit OpponentValueBaselineTable(const std::uint64_t maximum_states)
      : maximum_states_(maximum_states) {}

  [[nodiscard]] Result<std::array<double, maximum_actions>, HuPreflopError>
  frozen_values(const InformationKey &key, const std::size_t action_count) const {
    const auto found = key_to_index_.find(key);
    if (found == key_to_index_.end()) {
      return Result<std::array<double, maximum_actions>, HuPreflopError>::success({});
    }
    const auto &state = state_at(found->second);
    return state.action_count == action_count
               ? Result<std::array<double, maximum_actions>, HuPreflopError>::success(state.means)
               : Result<std::array<double, maximum_actions>, HuPreflopError>::failure(
                     HuPreflopError::IntegrityFailure);
  }

  bool observe(const InformationKey &key, const std::size_t action_count, const std::size_t action,
               const double value) {
    if (action >= action_count || !std::isfinite(value)) {
      valid_ = false;
      return false;
    }
    auto found = key_to_index_.find(key);
    if (found == key_to_index_.end()) {
      if (size_ >= maximum_states_) {
        valid_ = false;
        memory_exhausted_ = true;
        return false;
      }
      const auto index = append_state(key, action_count);
      if (!valid_) {
        return false;
      }
      found = key_to_index_.find(key);
      if (found == key_to_index_.end() || found->second != index) {
        valid_ = false;
        return false;
      }
    }
    auto &state = state_at(found->second);
    if (state.action_count != action_count) {
      valid_ = false;
      return false;
    }
    auto &count = state.sample_counts[action];
    auto &mean = state.means[action];
    ++count;
    mean += (value - mean) / static_cast<double>(count);
    valid_ = std::isfinite(mean);
    return valid_;
  }

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] HuPreflopError error() const noexcept {
    return memory_exhausted_ ? HuPreflopError::MemoryFailure : HuPreflopError::NumericalFailure;
  }
  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return size_ * (sizeof(InformationKey) + sizeof(OpponentValueBaselineState));
  }

private:
  static constexpr std::uint64_t state_chunk_capacity = 1'024U;

  struct StateChunk {
    std::unique_ptr<OpponentValueBaselineState[]> states;
    std::size_t capacity{0U};
    std::size_t used{0U};
  };

  OpponentValueBaselineState &state_at(const std::uint64_t index) {
    const auto chunk = static_cast<std::size_t>(index / state_chunk_capacity);
    const auto offset = static_cast<std::size_t>(index % state_chunk_capacity);
    return chunks_[chunk].states[offset];
  }

  const OpponentValueBaselineState &state_at(const std::uint64_t index) const {
    const auto chunk = static_cast<std::size_t>(index / state_chunk_capacity);
    const auto offset = static_cast<std::size_t>(index % state_chunk_capacity);
    return chunks_[chunk].states[offset];
  }

  std::uint64_t append_state(const InformationKey &key, const std::size_t action_count) {
    try {
      if (chunks_.empty() || chunks_.back().used == chunks_.back().capacity) {
        const auto capacity =
            static_cast<std::size_t>(std::min(state_chunk_capacity, maximum_states_ - size_));
        StateChunk chunk;
        chunk.states = std::make_unique<OpponentValueBaselineState[]>(capacity);
        chunk.capacity = capacity;
        chunks_.push_back(std::move(chunk));
      }
      auto &chunk = chunks_.back();
      const auto index = size_;
      chunk.states[chunk.used] = {};
      chunk.states[chunk.used].action_count = static_cast<std::uint8_t>(action_count);
      key_to_index_.emplace(key, index);
      ++chunk.used;
      ++size_;
      return index;
    } catch (const std::bad_alloc &) {
      valid_ = false;
      memory_exhausted_ = true;
      return 0U;
    }
  }

  std::unordered_map<InformationKey, std::uint64_t, InformationKeyHash> key_to_index_;
  std::vector<StateChunk> chunks_;
  std::uint64_t maximum_states_{0U};
  std::uint64_t size_{0U};
  bool valid_{true};
  bool memory_exhausted_{false};
};

class BoundedBucketCache {
public:
  void set_capacity(const std::uint64_t capacity) { capacity_ = capacity; }

  [[nodiscard]] std::optional<std::uint16_t> find(const std::uint64_t key) const {
    const auto found = values_.find(key);
    return found == values_.end() ? std::nullopt : std::optional<std::uint16_t>{found->second};
  }

  void insert(const std::uint64_t key, const std::uint16_t value) {
    if (capacity_ == 0U || values_.contains(key)) {
      return;
    }
    if (values_.size() >= capacity_) {
      values_.erase(insertion_order_.front());
      insertion_order_.pop_front();
      ++evictions_;
    }
    values_.emplace(key, value);
    insertion_order_.push_back(key);
    peak_entries_ = std::max<std::uint64_t>(peak_entries_, values_.size());
  }

  [[nodiscard]] std::uint64_t peak_entries() const noexcept { return peak_entries_; }
  [[nodiscard]] std::uint64_t evictions() const noexcept { return evictions_; }

private:
  std::unordered_map<std::uint64_t, std::uint16_t> values_;
  std::deque<std::uint64_t> insertion_order_;
  std::uint64_t capacity_{0U};
  std::uint64_t peak_entries_{0U};
  std::uint64_t evictions_{0U};
};

class BoundedExactAllInEquityCache {
public:
  void set_capacity(const std::uint64_t capacity) { capacity_ = capacity; }

  [[nodiscard]] std::optional<HuPreflopPostflopAllInEquity> find(const std::uint64_t key) const {
    const auto found = values_.find(key);
    return found == values_.end() ? std::nullopt
                                  : std::optional<HuPreflopPostflopAllInEquity>{found->second};
  }

  void insert(const std::uint64_t key, const HuPreflopPostflopAllInEquity &value) {
    if (capacity_ == 0U || values_.contains(key)) {
      return;
    }
    if (values_.size() >= capacity_) {
      values_.erase(insertion_order_.front());
      insertion_order_.pop_front();
      ++evictions_;
    }
    values_.emplace(key, value);
    insertion_order_.push_back(key);
    peak_entries_ = std::max<std::uint64_t>(peak_entries_, values_.size());
  }

  [[nodiscard]] std::uint64_t peak_entries() const noexcept { return peak_entries_; }
  [[nodiscard]] std::uint64_t evictions() const noexcept { return evictions_; }

private:
  std::unordered_map<std::uint64_t, HuPreflopPostflopAllInEquity> values_;
  std::deque<std::uint64_t> insertion_order_;
  std::uint64_t capacity_{0U};
  std::uint64_t peak_entries_{0U};
  std::uint64_t evictions_{0U};
};

std::uint64_t canonical_exact_all_in_equity_key(const Deal &deal, const Street street) {
  const auto visible_board_count = street == Street::Flop ? 3U : 4U;
  std::array<std::uint8_t, 4> permutation{0U, 1U, 2U, 3U};
  auto minimum_key = std::numeric_limits<std::uint64_t>::max();
  do {
    const auto transform = [&permutation](const CardId card) {
      return static_cast<std::uint8_t>(static_cast<std::uint8_t>(card.rank()) * 4U +
                                       permutation[static_cast<std::uint8_t>(card.suit())]);
    };
    std::array<std::uint8_t, 2> first{transform(deal.holes[0][0]), transform(deal.holes[0][1])};
    std::array<std::uint8_t, 2> second{transform(deal.holes[1][0]), transform(deal.holes[1][1])};
    std::array<std::uint8_t, 4> board{};
    for (std::size_t index = 0U; index < visible_board_count; ++index) {
      board[index] = transform(deal.board[index]);
    }
    std::sort(first.begin(), first.end());
    std::sort(second.begin(), second.end());
    std::sort(board.begin(), board.begin() + static_cast<std::ptrdiff_t>(visible_board_count));
    std::uint64_t key = street == Street::Flop ? 0U : 1U;
    const auto append = [&key](const std::uint8_t card) { key = (key << 6U) | card; };
    for (const auto card : first) {
      append(card);
    }
    for (const auto card : second) {
      append(card);
    }
    for (std::size_t index = 0U; index < visible_board_count; ++index) {
      append(board[index]);
    }
    minimum_key = std::min(minimum_key, key);
  } while (std::next_permutation(permutation.begin(), permutation.end()));
  return minimum_key;
}

double sampled_update_weight(const HuPreflopSamplingAlgorithm algorithm,
                             const std::uint64_t iteration) noexcept {
  return algorithm == HuPreflopSamplingAlgorithm::LinearMccfr ? static_cast<double>(iteration)
                                                              : 1.0;
}

std::string sampled_algorithm_id(const HuPreflopSamplingAlgorithm algorithm) {
  switch (algorithm) {
  case HuPreflopSamplingAlgorithm::ExternalSampling:
    return "external_sampling_v2_opponent_pass_average";
  case HuPreflopSamplingAlgorithm::LinearMccfr:
    return "linear_mccfr_v1_opponent_pass_average";
  case HuPreflopSamplingAlgorithm::DiscountedMccfr1503:
    return "external_sampling_dcfr_1.5_0_3_alternating_v2_opponent_pass_average";
  case HuPreflopSamplingAlgorithm::ChanceSampledCfr:
    return "chance_sampled_full_action_cfr_v1_alternating";
  }
  return "unknown";
}

std::string chance_sampling_id(const HuPreflopChanceSamplingMode mode) {
  switch (mode) {
  case HuPreflopChanceSamplingMode::IndependentPhysical:
    return "independent_physical_deal_v1";
  case HuPreflopChanceSamplingMode::PublicBoardStratified:
    return "public_board_stratified_physical_deal_v1";
  }
  return "unknown";
}

std::string postflop_all_in_expectation_id(const HuPreflopPostflopAllInExpectationMode mode) {
  switch (mode) {
  case HuPreflopPostflopAllInExpectationMode::SampledRunout:
    return "sampled_runout_v1";
  case HuPreflopPostflopAllInExpectationMode::ExactTurn:
    return "exact_turn_runouts_v1";
  case HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn:
    return "exact_flop_turn_runouts_v1";
  }
  return "unknown";
}

bool is_distributional_strength_representation(
    const HuPreflopPostflopRepresentation representation) noexcept {
  return representation == HuPreflopPostflopRepresentation::DistributionalStrengthPrototype ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthPerfectRecall ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6 ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23;
}

bool uses_distributional_profile_v6(const HuPreflopPostflopRepresentation representation) noexcept {
  return representation == HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6;
}

bool uses_distributional_structured_v7(
    const HuPreflopPostflopRepresentation representation) noexcept {
  return representation == HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10;
}

bool uses_distributional_street_adaptive_v8(
    const HuPreflopPostflopRepresentation representation) noexcept {
  return representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23;
}

bool representation_uses_current_street_only(
    const HuPreflopPostflopRepresentation representation) noexcept {
  return representation == HuPreflopPostflopRepresentation::CategoryEquityMonteCarloCurrentStreet ||
         representation == HuPreflopPostflopRepresentation::CategoryEquityMonteCarloMemoryless ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthPrototype ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6 ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8;
}

bool representation_forgets_preflop_class(
    const HuPreflopPostflopRepresentation representation) noexcept {
  return representation == HuPreflopPostflopRepresentation::CategoryEquityMonteCarloMemoryless ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthPrototype ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6 ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10 ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11 ||
         representation == HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory;
}

bool uses_distributional_selective_history_v9(
    const HuPreflopPostflopRepresentation representation) noexcept {
  return representation ==
         HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9;
}

bool uses_distributional_category_history_v10(
    const HuPreflopPostflopRepresentation representation) noexcept {
  return representation ==
         HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10;
}

bool uses_one_street_history(const HuPreflopPostflopRepresentation representation) noexcept {
  return uses_distributional_selective_history_v9(representation) ||
         uses_distributional_category_history_v10(representation) ||
         representation ==
             HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11;
}

std::uint16_t structured_category_from_bucket(const std::uint16_t bucket,
                                              const std::uint16_t capacity) noexcept {
  const auto capacity_bits =
      static_cast<std::uint32_t>(std::countr_zero(static_cast<std::uint32_t>(capacity)));
  return static_cast<std::uint16_t>(bucket >> (capacity_bits - 4U));
}

bool supported_sampled_policy_version(const HuPreflopSampledPostflopPolicy &policy) noexcept {
  return policy.major == HuPreflopSampledPostflopPolicy::format_major &&
         policy.minor >= HuPreflopSampledPostflopPolicy::minimum_supported_minor &&
         policy.minor <= HuPreflopSampledPostflopPolicy::format_minor &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
          policy.minor >= 6U) &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 ||
          policy.minor >= 7U) &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9 ||
          policy.minor >= 8U) &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10 ||
          policy.minor >= 9U) &&
         (policy.representation !=
              HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11 ||
          policy.minor >= 10U) &&
         (policy.representation != HuPreflopPostflopRepresentation::
                                       DistributionalStrengthStreetAdaptivePerfectRecallV23 ||
          policy.minor >= 12U) &&
         (policy.minor != 12U || policy.representation ==
                                     HuPreflopPostflopRepresentation::
                                         DistributionalStrengthStreetAdaptivePerfectRecallV23);
}

bool sampled_policy_key_less(const HuPreflopSampledPostflopPolicyKey &left,
                             const HuPreflopSampledPostflopPolicyKey &right) noexcept {
  if (left.public_history != right.public_history) {
    return left.public_history < right.public_history;
  }
  if (left.physical_cards != right.physical_cards) {
    return left.physical_cards < right.physical_cards;
  }
  if (left.bucket_history != right.bucket_history) {
    return left.bucket_history < right.bucket_history;
  }
  if (left.preflop_class != right.preflop_class) {
    return left.preflop_class < right.preflop_class;
  }
  if (left.player != right.player) {
    return left.player < right.player;
  }
  return static_cast<std::uint8_t>(left.street) < static_cast<std::uint8_t>(right.street);
}

void mix_policy_fingerprint_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= policy_fingerprint_prime;
}

template <typename Unsigned>
void mix_policy_fingerprint_unsigned(std::uint64_t &hash, Unsigned value) noexcept {
  static_assert(std::is_unsigned_v<Unsigned>);
  for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    mix_policy_fingerprint_byte(hash, static_cast<std::uint8_t>(value & 0xFFU));
    value >>= 8U;
  }
}

void mix_policy_fingerprint_string(std::uint64_t &hash, const std::string &value) noexcept {
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint64_t>(value.size()));
  for (const auto character : value) {
    mix_policy_fingerprint_byte(hash, static_cast<std::uint8_t>(character));
  }
}

void mix_policy_fingerprint_double(std::uint64_t &hash, const double value) noexcept {
  mix_policy_fingerprint_unsigned(hash, std::bit_cast<std::uint64_t>(value));
}

std::string finish_policy_fingerprint(const std::uint64_t hash) {
  std::ostringstream stream;
  stream << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

std::string postflop_abstraction_id(const HuPreflopSolveOptions &options) {
  if (options.postflop_representation == HuPreflopPostflopRepresentation::ExactPhysical) {
    return "preflop_exact81_postflop_physical_lossless_suit_isomorphism_v2";
  }
  if (is_distributional_strength_representation(options.postflop_representation)) {
    return "preflop_exact81_postflop_distributional_strength_mc" +
           std::to_string(options.equity_samples_per_bucket) + "_capacity_" +
           std::to_string(options.distributional_bucket_capacities[0]) + "_" +
           std::to_string(options.distributional_bucket_capacities[1]) + "_" +
           std::to_string(options.distributional_bucket_capacities[2]) +
           (options.postflop_representation ==
                    HuPreflopPostflopRepresentation::
                        DistributionalStrengthStreetAdaptivePerfectRecallV23
                ? "_street_adaptive_category_equity_profile_v8_full_history_v23_perfect_recall"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::
                        DistributionalStrengthAdaptiveCategoryHistoryV11
                ? "_street_adaptive_category_equity_profile_category_history_v11_imperfect_recall"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10
                ? "_category_equity_ordered_profile_category_history_v10_imperfect_recall"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9
                ? "_category_equity_ordered_profile_selective_history_v9_imperfect_recall"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8
                ? "_street_adaptive_category_equity_profile_v8_current_observation_imperfect_recall"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7
                ? "_category_equity_ordered_profile_v7_current_observation_imperfect_recall"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6
                ? "_profile_hash_v6_current_observation_imperfect_recall"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::DistributionalStrengthPerfectRecall
                ? "_hierarchical_feature_perfect_recall_v5"
            : options.postflop_representation ==
                    HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory
                ? "_hierarchical_feature_bucket_history_imperfect_recall_v5"
                : "_current_observation_imperfect_recall_v2");
  }
  if (options.postflop_representation ==
      HuPreflopPostflopRepresentation::CategoryEquityMonteCarloMemoryless) {
    return "preflop_exact81_postflop_category_equity_mc" +
           std::to_string(options.equity_samples_per_bucket) +
           "_memoryless_imperfect_recall_suit_invariant_v2";
  }
  if (options.postflop_representation ==
      HuPreflopPostflopRepresentation::CategoryEquityMonteCarloCurrentStreet) {
    return "preflop_exact81_postflop_category_equity_mc" +
           std::to_string(options.equity_samples_per_bucket) +
           "_current_street_imperfect_recall_suit_invariant_v2";
  }
  return "preflop_exact81_postflop_category_equity_mc" +
         std::to_string(options.equity_samples_per_bucket) + "_perfect_recall_suit_invariant_v2";
}

std::string abstract_game_rules_fingerprint(const HuPreflopConfig &config) {
  auto hash = policy_fingerprint_offset;
  mix_policy_fingerprint_string(
      hash, "gtosd.hu_preflop_rules.v1|short_deck_36_flush_over_full_house_a6789");
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint64_t>(config.effective_stack.units()));
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint64_t>(config.ante.units()));
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(config.rake.enabled));
  mix_policy_fingerprint_unsigned(hash, config.rake.percentage.basis_points());
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint64_t>(config.rake.cap.units()));
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(config.rake.no_flop_no_drop));
  mix_policy_fingerprint_unsigned(hash,
                                  static_cast<std::uint64_t>(config.rake.minimum_pot.units()));
  return finish_policy_fingerprint(hash);
}

std::string
abstract_game_abstraction_fingerprint(const HuPreflopAbstractGameDefinition &definition) {
  auto hash = policy_fingerprint_offset;
  mix_policy_fingerprint_string(hash, "gtosd.hu_preflop_abstraction.v1");
  mix_policy_fingerprint_string(hash, definition.abstraction_id);
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(definition.representation));
  mix_policy_fingerprint_unsigned(hash, definition.partition_seed);
  mix_policy_fingerprint_unsigned(hash, definition.equity_samples_per_bucket);
  for (const auto capacity : definition.distributional_bucket_capacities) {
    mix_policy_fingerprint_unsigned(hash, capacity);
  }
  mix_policy_fingerprint_unsigned(
      hash, static_cast<std::uint8_t>(definition.recall_contract.retains_complete_public_history));
  mix_policy_fingerprint_unsigned(
      hash, static_cast<std::uint8_t>(definition.recall_contract.retains_preflop_class));
  for (const auto &decision : definition.recall_contract.retains_bucket_observation) {
    for (const auto retained : decision) {
      mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(retained));
    }
  }
  return finish_policy_fingerprint(hash);
}

bool checked_multiply_or_saturate(const std::uint64_t left, const std::uint64_t right,
                                  std::uint64_t &output) noexcept {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    output = std::numeric_limits<std::uint64_t>::max();
    return false;
  }
  output = left * right;
  return true;
}

bool checked_add_or_saturate(const std::uint64_t left, const std::uint64_t right,
                             std::uint64_t &output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    output = std::numeric_limits<std::uint64_t>::max();
    return false;
  }
  output = left + right;
  return true;
}

bool same_preflop_tree_stats(const HuPreflopTreeStats &left,
                             const HuPreflopTreeStats &right) noexcept {
  return left.node_count == right.node_count && left.edge_count == right.edge_count &&
         left.decision_nodes == right.decision_nodes &&
         left.postflop_entries == right.postflop_entries &&
         left.terminal_folds == right.terminal_folds &&
         left.terminal_all_ins == right.terminal_all_ins &&
         left.maximum_depth == right.maximum_depth;
}

bool same_postflop_public_stats(const HuPostflopPublicStats &left,
                                const HuPostflopPublicStats &right) noexcept {
  return left.represented_nodes == right.represented_nodes &&
         left.action_edges == right.action_edges && left.decision_nodes == right.decision_nodes &&
         left.decision_nodes_by_street == right.decision_nodes_by_street &&
         left.chance_frontiers == right.chance_frontiers &&
         left.terminal_folds == right.terminal_folds &&
         left.terminal_showdowns == right.terminal_showdowns &&
         left.terminal_all_in_runouts == right.terminal_all_in_runouts &&
         left.memoized_states == right.memoized_states &&
         left.maximum_subtree_depth == right.maximum_subtree_depth &&
         left.maximum_observed_raise_count == right.maximum_observed_raise_count &&
         left.core_raise_safety_limit_reached == right.core_raise_safety_limit_reached &&
         left.natural_stack_termination_proven == right.natural_stack_termination_proven;
}

bool valid_sampled_policy_key(const HuPreflopSampledPostflopPolicyKey &key,
                              const HuPreflopPostflopRepresentation representation) noexcept {
  if (key.player > 1U || key.preflop_class >= hu_preflop_hand_class_count ||
      key.street < Street::Flop || key.street > Street::River) {
    return false;
  }
  const auto street_index =
      static_cast<std::size_t>(key.street) - static_cast<std::size_t>(Street::Flop);
  if (representation == HuPreflopPostflopRepresentation::ExactPhysical) {
    return key.physical_cards != std::numeric_limits<std::uint64_t>::max() &&
           std::ranges::all_of(key.bucket_history,
                               [](const auto bucket) { return bucket == unset_bucket; });
  }
  if (key.physical_cards != std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  const bool current_street_only = representation_uses_current_street_only(representation);
  for (std::size_t index = 0U; index < key.bucket_history.size(); ++index) {
    const bool selective_history = uses_one_street_history(representation);
    const bool expected = selective_history ? index <= street_index &&
                                                  (street_index == 0U || index + 1U >= street_index)
                          : current_street_only ? index == street_index
                                                : index <= street_index;
    if ((key.bucket_history[index] != unset_bucket) != expected) {
      return false;
    }
  }
  if ((uses_distributional_category_history_v10(representation) ||
       representation ==
           HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11) &&
      street_index > 0U &&
      key.bucket_history[street_index - 1U] >
          static_cast<std::uint16_t>(HandCategory::StraightFlush)) {
    return false;
  }
  return !representation_forgets_preflop_class(representation) || key.preflop_class == 0U;
}

std::uint64_t mix_history(std::uint64_t history, const std::uint64_t value) {
  history ^= value + 0x9E37'79B9'7F4A'7C15ULL + (history << 6U) + (history >> 2U);
  history ^= history >> 30U;
  history *= 0xBF58'476D'1CE4'E5B9ULL;
  history ^= history >> 27U;
  return history;
}

std::size_t sample_action(const std::array<double, maximum_actions> &strategy,
                          const std::size_t action_count, std::mt19937_64 &random) {
  const auto draw = std::generate_canonical<double, 53>(random);
  return external_sampling_action_from_quantile(strategy, action_count, draw);
}

double root_response_stratified_quantile(const std::uint64_t traversal_seed,
                                         const std::uint64_t iteration, const std::uint8_t rollout,
                                         const std::uint8_t rollout_count) {
  const auto seed =
      mix_history(traversal_seed ^ 0x5253'504E'5354'5254ULL, (iteration << 8U) | rollout);
  std::mt19937_64 random(seed);
  const auto within_stratum = std::generate_canonical<double, 53>(random);
  return external_sampling_stratified_quantile(rollout, rollout_count, within_stratum);
}

Deal sample_deal(std::mt19937_64 &random) {
  auto deck = short_deck();
  std::shuffle(deck.begin(), deck.end(), random);
  Deal deal;
  deal.holes[0] = {deck[0], deck[1]};
  deal.holes[1] = {deck[2], deck[3]};
  std::copy_n(deck.begin() + 4, 5, deal.board.begin());
  return deal;
}

Result<Deal, HuPreflopError> sample_deal_conditioned_on_class(const HandClassId hand_class_id,
                                                              const std::uint8_t traverser,
                                                              std::mt19937_64 &random) {
  if (hand_class_id >= hu_preflop_hand_class_count || traverser > 1U) {
    return Result<Deal, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  static const auto combos_by_class = [] {
    std::array<std::vector<Combo>, hu_preflop_hand_class_count> grouped;
    for (const auto combo : all_combos()) {
      grouped[hand_class(combo)].push_back(combo);
    }
    return grouped;
  }();
  const auto &eligible = combos_by_class[hand_class_id];
  if (eligible.empty()) {
    return Result<Deal, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto &own =
      eligible[std::uniform_int_distribution<std::size_t>(0U, eligible.size() - 1U)(random)];
  std::array<CardId, 34> remaining{};
  std::size_t next = 0U;
  for (const auto card : short_deck()) {
    if (card != own.first && card != own.second) {
      remaining[next++] = card;
    }
  }
  if (next != remaining.size()) {
    return Result<Deal, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  std::shuffle(remaining.begin(), remaining.end(), random);
  Deal deal;
  deal.holes[traverser] = {own.first, own.second};
  deal.holes[1U - traverser] = {remaining[0], remaining[1]};
  std::copy_n(remaining.begin() + 2, deal.board.size(), deal.board.begin());
  return Result<Deal, HuPreflopError>::success(std::move(deal));
}

std::array<CardId, 2> combination_at(const std::span<const CardId> cards,
                                     std::size_t combination_index) {
  for (std::size_t first = 0U; first + 1U < cards.size(); ++first) {
    const auto combinations_with_first = cards.size() - first - 1U;
    if (combination_index < combinations_with_first) {
      return {cards[first], cards[first + 1U + combination_index]};
    }
    combination_index -= combinations_with_first;
  }
  return {};
}

Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>
sample_public_board_private_deals(const std::array<CardId, 5> &board, const std::uint8_t traverser,
                                  const std::uint32_t sample_count, std::mt19937_64 &random) {
  constexpr std::size_t compatible_hole_combinations = (31U * 30U) / 2U;
  if (traverser > 1U || sample_count == 0U || sample_count > compatible_hole_combinations) {
    return Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  std::uint64_t board_mask = 0U;
  for (const auto card : board) {
    if (card.value() >= 36U || (board_mask & card.mask()) != 0U) {
      return Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
    board_mask |= card.mask();
  }
  std::array<CardId, 31> available{};
  std::size_t available_count = 0U;
  for (const auto card : short_deck()) {
    if ((board_mask & card.mask()) == 0U) {
      available[available_count++] = card;
    }
  }
  if (available_count != available.size()) {
    return Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  try {
    std::vector<HuPreflopPublicBoardPrivateDeal> deals(sample_count);
    const auto offset =
        std::uniform_int_distribution<std::size_t>(0U, compatible_hole_combinations - 1U)(random);
    for (std::size_t sample = 0U; sample < sample_count; ++sample) {
      const auto own_hole =
          combination_at(available, (offset + sample) % compatible_hole_combinations);
      std::array<CardId, 29> opponent_available{};
      std::size_t opponent_available_count = 0U;
      for (const auto card : available) {
        if (card != own_hole[0] && card != own_hole[1]) {
          opponent_available[opponent_available_count++] = card;
        }
      }
      const auto opponent_combination_count =
          (opponent_available.size() * (opponent_available.size() - 1U)) / 2U;
      const auto opponent_hole = combination_at(
          opponent_available,
          std::uniform_int_distribution<std::size_t>(0U, opponent_combination_count - 1U)(random));
      deals[sample].holes[traverser] = own_hole;
      deals[sample].holes[1U - traverser] = opponent_hole;
    }
    return Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>::success(
        std::move(deals));
  } catch (const std::bad_alloc &) {
    return Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

struct CanonicalVisibleObservation {
  std::array<CardId, 2> hole{};
  std::array<CardId, 5> board{};
  std::uint64_t key{0U};
};

CanonicalVisibleObservation
canonical_visible_observation(const Deal &deal, const std::uint8_t player, const Street street) {
  const auto transform = [](const CardId card, const std::array<std::uint8_t, 4> &permutation) {
    return CardId::from_index(
               static_cast<std::uint8_t>((card.value() / 4U) * 4U + permutation[card.value() % 4U]))
        .value();
  };
  CanonicalVisibleObservation best;
  best.key = std::numeric_limits<std::uint64_t>::max();
  std::array<std::uint8_t, 4> permutation{0U, 1U, 2U, 3U};
  do {
    std::array<CardId, 2> hole{transform(deal.holes[player][0], permutation),
                               transform(deal.holes[player][1], permutation)};
    std::array<CardId, 3> flop{transform(deal.board[0], permutation),
                               transform(deal.board[1], permutation),
                               transform(deal.board[2], permutation)};
    std::ranges::sort(hole);
    std::ranges::sort(flop);
    std::uint64_t key = 0U;
    for (const auto card : hole) {
      key = (key << 6U) | card.value();
    }
    for (const auto card : flop) {
      key = (key << 6U) | card.value();
    }
    std::array<CardId, 5> board{flop[0], flop[1], flop[2], deal.board[3], deal.board[4]};
    if (street >= Street::Turn) {
      board[3] = transform(deal.board[3], permutation);
      key = (key << 6U) | board[3].value();
    }
    if (street >= Street::River) {
      board[4] = transform(deal.board[4], permutation);
      key = (key << 6U) | board[4].value();
    }
    if (key < best.key) {
      best = {hole, board, key};
    }
  } while (std::ranges::next_permutation(permutation).found);
  return best;
}

std::uint64_t physical_cards_key(const Deal &deal, const std::uint8_t player, const Street street) {
  return canonical_visible_observation(deal, player, street).key;
}

std::uint64_t visible_mask(const Deal &deal, const std::uint8_t player,
                           const std::size_t board_count) {
  std::uint64_t mask = deal.holes[player][0].mask() | deal.holes[player][1].mask();
  for (std::size_t index = 0; index < board_count; ++index) {
    mask |= deal.board[index].mask();
  }
  return mask;
}

std::uint64_t splitmix64(std::uint64_t &state) {
  state += 0x9E37'79B9'7F4A'7C15ULL;
  auto value = state;
  value = (value ^ (value >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D0'49BB'1331'11EBULL;
  return value ^ (value >> 31U);
}

Result<HandCategory, HuPreflopError> visible_category(const Deal &deal, const std::uint8_t player,
                                                      const std::size_t board_count) {
  std::array<CardId, 7> cards{};
  cards[0] = deal.holes[player][0];
  cards[1] = deal.holes[player][1];
  for (std::size_t index = 0; index < board_count; ++index) {
    cards[index + 2U] = deal.board[index];
  }
  if (board_count == 3U) {
    const std::array<CardId, 5> five{cards[0], cards[1], cards[2], cards[3], cards[4]};
    const auto value = evaluate_five(five);
    return value ? Result<HandCategory, HuPreflopError>::success(value.value().category)
                 : Result<HandCategory, HuPreflopError>::failure(HuPreflopError::EquityFailure);
  }
  if (board_count == 5U) {
    const auto value = evaluate_seven(cards);
    return value ? Result<HandCategory, HuPreflopError>::success(value.value().category)
                 : Result<HandCategory, HuPreflopError>::failure(HuPreflopError::EquityFailure);
  }
  HandValue best{};
  bool initialized = false;
  for (std::size_t omitted = 0; omitted < 6U; ++omitted) {
    std::array<CardId, 5> five{};
    std::size_t target = 0;
    for (std::size_t source = 0; source < 6U; ++source) {
      if (source != omitted) {
        five[target++] = cards[source];
      }
    }
    const auto value = evaluate_five(five);
    if (!value) {
      return Result<HandCategory, HuPreflopError>::failure(HuPreflopError::EquityFailure);
    }
    if (!initialized || value.value() > best) {
      best = value.value();
      initialized = true;
    }
  }
  return Result<HandCategory, HuPreflopError>::success(best.category);
}

Result<std::uint16_t, HuPreflopError>
exact_visible_category_code(const Deal &deal, const std::uint8_t player, const Street street) {
  if (player > 1U || street < Street::Flop || street > Street::River) {
    return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto board_count = street == Street::Flop ? 3U : street == Street::Turn ? 4U : 5U;
  if (static_cast<std::size_t>(std::popcount(visible_mask(deal, player, board_count))) !=
      board_count + 2U) {
    return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto category = visible_category(deal, player, board_count);
  return category ? Result<std::uint16_t, HuPreflopError>::success(
                        static_cast<std::uint16_t>(category.value()))
                  : Result<std::uint16_t, HuPreflopError>::failure(category.error());
}

Result<std::uint16_t, HuPreflopError> distributional_information_bucket(
    const Deal &deal, const std::uint8_t player, const Street bucket_street,
    const std::size_t bucket_index, const std::size_t final_index, const std::uint16_t bucket,
    const std::uint16_t capacity, const HuPreflopPostflopRepresentation representation) {
  if (bucket_index >= final_index) {
    return Result<std::uint16_t, HuPreflopError>::success(bucket);
  }
  if (representation ==
      HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11) {
    return exact_visible_category_code(deal, player, bucket_street);
  }
  if (uses_distributional_category_history_v10(representation)) {
    return Result<std::uint16_t, HuPreflopError>::success(
        structured_category_from_bucket(bucket, capacity));
  }
  return Result<std::uint16_t, HuPreflopError>::success(bucket);
}

Result<std::uint16_t, HuPreflopError> compute_bucket(const Deal &deal, const std::uint8_t player,
                                                     const Street street,
                                                     const std::uint32_t samples,
                                                     const std::uint64_t bucket_seed) {
  if (player > 1U || street < Street::Flop || street > Street::River || samples == 0U) {
    return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto board_count = street == Street::Flop ? 3U : street == Street::Turn ? 4U : 5U;
  if (static_cast<std::size_t>(std::popcount(visible_mask(deal, player, board_count))) !=
      board_count + 2U) {
    return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto canonical = canonical_visible_observation(deal, player, street);
  Deal visible_deal;
  visible_deal.holes[player] = canonical.hole;
  visible_deal.board = canonical.board;
  const auto category = visible_category(visible_deal, player, board_count);
  if (!category) {
    return Result<std::uint16_t, HuPreflopError>::failure(category.error());
  }
  const auto dead = visible_mask(visible_deal, player, board_count);
  std::array<CardId, 36> available{};
  std::size_t available_count = 0;
  for (const auto card : short_deck()) {
    if ((dead & card.mask()) == 0U) {
      available[available_count++] = card;
    }
  }
  const auto runout_count = 5U - board_count;
  double equity_points = 0.0;
  auto random_state = canonical.key ^ (static_cast<std::uint64_t>(street) << 57U) ^ bucket_seed;
  for (std::uint32_t sample = 0; sample < samples; ++sample) {
    auto pool = available;
    const auto needed = 2U + runout_count;
    for (std::size_t selected = 0; selected < needed; ++selected) {
      const auto remaining = available_count - selected;
      const auto offset = static_cast<std::size_t>(splitmix64(random_state) % remaining);
      std::swap(pool[selected], pool[selected + offset]);
    }
    std::array<CardId, 5> complete_board = visible_deal.board;
    for (std::size_t index = 0; index < runout_count; ++index) {
      complete_board[board_count + index] = pool[2U + index];
    }
    std::array<CardId, 7> hero{visible_deal.holes[player][0],
                               visible_deal.holes[player][1],
                               complete_board[0],
                               complete_board[1],
                               complete_board[2],
                               complete_board[3],
                               complete_board[4]};
    std::array<CardId, 7> opponent{pool[0],           pool[1],           complete_board[0],
                                   complete_board[1], complete_board[2], complete_board[3],
                                   complete_board[4]};
    const auto hero_value = evaluate_seven(hero);
    const auto opponent_value = evaluate_seven(opponent);
    if (!hero_value || !opponent_value) {
      return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::EquityFailure);
    }
    if (hero_value.value() > opponent_value.value()) {
      equity_points += 1.0;
    } else if (hero_value.value() == opponent_value.value()) {
      equity_points += 0.5;
    }
  }
  const std::uint16_t bins = street == Street::Flop ? 16U : street == Street::Turn ? 24U : 32U;
  const auto equity = equity_points / static_cast<double>(samples);
  const auto equity_bin = static_cast<std::uint16_t>(
      std::min<double>(bins - 1U, std::floor(equity * static_cast<double>(bins))));
  return Result<std::uint16_t, HuPreflopError>::success(
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(category.value()) * bins + equity_bin));
}

Result<std::uint16_t, HuPreflopError> compute_distributional_strength_bucket(
    const Deal &deal, const std::uint8_t player, const Street street, const std::uint32_t samples,
    const std::uint64_t partition_seed, const std::array<std::uint16_t, 3> &capacities,
    const bool profile_v6, const bool structured_v7, const bool street_adaptive_v8) {
  if (player > 1U || street < Street::Flop || street > Street::River || samples == 0U ||
      static_cast<unsigned>(profile_v6) + static_cast<unsigned>(structured_v7) +
              static_cast<unsigned>(street_adaptive_v8) >
          1U) {
    return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto board_count = street == Street::Flop ? 3U : street == Street::Turn ? 4U : 5U;
  if (static_cast<std::size_t>(std::popcount(visible_mask(deal, player, board_count))) !=
      board_count + 2U) {
    return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto canonical = canonical_visible_observation(deal, player, street);
  Deal visible_deal;
  visible_deal.holes[player] = canonical.hole;
  visible_deal.board = canonical.board;
  const auto category = visible_category(visible_deal, player, board_count);
  if (!category) {
    return Result<std::uint16_t, HuPreflopError>::failure(category.error());
  }

  std::array<std::uint8_t, 4> suit_counts{};
  std::array<std::uint8_t, 9> rank_counts{};
  for (std::size_t index = 0U; index < board_count; ++index) {
    ++suit_counts[static_cast<std::size_t>(visible_deal.board[index].suit())];
    ++rank_counts[static_cast<std::size_t>(visible_deal.board[index].rank())];
  }
  const auto maximum_suit_count = *std::ranges::max_element(suit_counts);
  const auto paired_board =
      std::ranges::any_of(rank_counts, [](const auto count) { return count >= 2U; });

  const auto dead = visible_mask(visible_deal, player, board_count);
  std::array<CardId, 36> available{};
  std::size_t available_count = 0U;
  for (const auto card : short_deck()) {
    if ((dead & card.mask()) == 0U) {
      available[available_count++] = card;
    }
  }
  const auto runout_count = 5U - board_count;
  double equity_points = 0.0;
  double squared_equity_points = 0.0;
  std::array<double, 3> opponent_group_points{};
  std::array<std::uint32_t, 3> opponent_group_counts{};
  std::array<std::uint32_t, 9> future_category_counts{};
  auto random_state = canonical.key ^ (static_cast<std::uint64_t>(street) << 57U) ^ partition_seed;
  for (std::uint32_t sample = 0U; sample < samples; ++sample) {
    auto pool = available;
    const auto needed = 2U + runout_count;
    for (std::size_t selected = 0U; selected < needed; ++selected) {
      const auto remaining = available_count - selected;
      const auto offset = static_cast<std::size_t>(splitmix64(random_state) % remaining);
      std::swap(pool[selected], pool[selected + offset]);
    }
    std::array<CardId, 5> complete_board = visible_deal.board;
    for (std::size_t index = 0U; index < runout_count; ++index) {
      complete_board[board_count + index] = pool[2U + index];
    }
    const std::array<CardId, 7> hero{visible_deal.holes[player][0],
                                     visible_deal.holes[player][1],
                                     complete_board[0],
                                     complete_board[1],
                                     complete_board[2],
                                     complete_board[3],
                                     complete_board[4]};
    const std::array<CardId, 7> opponent{pool[0],           pool[1],           complete_board[0],
                                         complete_board[1], complete_board[2], complete_board[3],
                                         complete_board[4]};
    const auto hero_value = evaluate_seven(hero);
    const auto opponent_value = evaluate_seven(opponent);
    if (!hero_value || !opponent_value) {
      return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::EquityFailure);
    }
    const auto hero_category = static_cast<std::size_t>(hero_value.value().category);
    const auto opponent_category = static_cast<std::size_t>(opponent_value.value().category);
    ++future_category_counts[hero_category];
    const auto opponent_group = std::min<std::size_t>(2U, opponent_category / 3U);
    ++opponent_group_counts[opponent_group];
    const auto point = hero_value.value() > opponent_value.value()    ? 1.0
                       : hero_value.value() == opponent_value.value() ? 0.5
                                                                      : 0.0;
    equity_points += point;
    squared_equity_points += point * point;
    opponent_group_points[opponent_group] += point;
  }

  const auto sample_count = static_cast<double>(samples);
  const auto equity = equity_points / sample_count;
  const auto variance = std::max(0.0, squared_equity_points / sample_count - equity * equity);
  const auto equity_bin = static_cast<std::uint64_t>(std::min(15.0, std::floor(equity * 16.0)));
  const auto variance_bin = static_cast<std::uint64_t>(std::min(7.0, std::floor(variance * 32.0)));
  const auto dominant_future_category = static_cast<std::uint64_t>(
      std::ranges::max_element(future_category_counts) - future_category_counts.begin());

  std::array<std::uint64_t, 3> opponent_group_bins{};
  for (std::size_t group = 0U; group < opponent_group_counts.size(); ++group) {
    const auto group_equity =
        opponent_group_counts[group] == 0U
            ? 0.5
            : opponent_group_points[group] / static_cast<double>(opponent_group_counts[group]);
    opponent_group_bins[group] =
        static_cast<std::uint64_t>(std::min(3.0, std::floor(group_equity * 4.0)));
  }
  const auto capacity =
      capacities[static_cast<std::size_t>(street) - static_cast<std::size_t>(Street::Flop)];
  if (capacity < 2U || capacity > 32'768U || !std::has_single_bit(capacity)) {
    return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto opponent_profile = std::min<std::uint64_t>(
      3U, (opponent_group_bins[0] + opponent_group_bins[1] + opponent_group_bins[2] + 1U) / 3U);
  const auto dominant_group = std::min<std::uint64_t>(3U, dominant_future_category / 3U);
  const auto texture = static_cast<std::uint64_t>(paired_board != (maximum_suit_count >= 3U));
  const auto feature_code = (static_cast<std::uint64_t>(category.value()) << 12U) |
                            (equity_bin << 8U) | (opponent_profile << 6U) | (variance_bin << 3U) |
                            (dominant_group << 1U) | texture;
  const auto capacity_bits =
      static_cast<std::uint32_t>(std::countr_zero(static_cast<std::uint32_t>(capacity)));
  if (street_adaptive_v8) {
    const auto category_index = static_cast<std::uint64_t>(category.value());
    const auto category_bits = street == Street::Flop ? 2U : street == Street::Turn ? 3U : 4U;
    if (capacity_bits <= category_bits) {
      return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    std::uint64_t category_group = category_index;
    if (street == Street::Flop) {
      switch (category.value()) {
      case HandCategory::HighCard:
      case HandCategory::Pair:
        category_group = 0U;
        break;
      case HandCategory::TwoPair:
      case HandCategory::ThreeOfAKind:
        category_group = 1U;
        break;
      case HandCategory::Straight:
      case HandCategory::Flush:
        category_group = 2U;
        break;
      case HandCategory::FullHouse:
      case HandCategory::FourOfAKind:
      case HandCategory::StraightFlush:
        category_group = 3U;
        break;
      }
    } else if (street == Street::Turn) {
      category_group = std::min<std::uint64_t>(7U, category_index);
    }
    const auto within_group_bits = capacity_bits - category_bits;
    const auto strength_bits = std::min<std::uint32_t>(4U, within_group_bits);
    const auto profile_bits = within_group_bits - strength_bits;
    const auto strength_bucket = equity_bin >> (4U - strength_bits);

    constexpr std::uint64_t profile_score_domain = 43U;
    const auto profile_score = 2U * opponent_group_bins[0] + 3U * opponent_group_bins[1] +
                               2U * opponent_group_bins[2] + 2U * variance_bin +
                               2U * dominant_group + texture;
    const auto profile_count = std::uint64_t{1U} << profile_bits;
    const auto profile_bucket =
        profile_bits == 0U
            ? 0U
            : std::min(profile_count - 1U, profile_score * profile_count / profile_score_domain);
    const auto within_group = (strength_bucket << profile_bits) | profile_bucket;
    return Result<std::uint16_t, HuPreflopError>::success(
        static_cast<std::uint16_t>((category_group << within_group_bits) | within_group));
  }
  if (structured_v7) {
    constexpr std::uint32_t category_bits = 4U;
    if (capacity_bits <= category_bits) {
      return Result<std::uint16_t, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    const auto within_category_bits = capacity_bits - category_bits;
    const auto profile_bits = std::max((within_category_bits - 1U) / 2U,
                                       within_category_bits > 4U ? within_category_bits - 4U : 0U);
    const auto strength_bits = within_category_bits - profile_bits;
    const auto retained_strength_bits = std::min<std::uint32_t>(4U, strength_bits);
    const auto strength_bucket = equity_bin >> (4U - retained_strength_bits);

    // Ordered integer projection of the already sampled distributional features.
    // The denominator is one above the maximum score, so the upper endpoint
    // always remains inside the requested profile domain.
    constexpr std::uint64_t profile_score_domain = 43U;
    const auto profile_score = 2U * opponent_group_bins[0] + 3U * opponent_group_bins[1] +
                               2U * opponent_group_bins[2] + 2U * variance_bin +
                               2U * dominant_group + texture;
    const auto profile_count = std::uint64_t{1U} << profile_bits;
    const auto profile_bucket =
        profile_bits == 0U
            ? 0U
            : std::min(profile_count - 1U, profile_score * profile_count / profile_score_domain);
    const auto within_category = (strength_bucket << profile_bits) | profile_bucket;
    return Result<std::uint16_t, HuPreflopError>::success(static_cast<std::uint16_t>(
        (static_cast<std::uint64_t>(category.value()) << within_category_bits) | within_category));
  }
  if (profile_v6) {
    const auto strength_bits = std::min<std::uint32_t>(4U, capacity_bits);
    const auto profile_bits = capacity_bits - strength_bits;
    const auto strength_bucket = equity_bin >> (4U - strength_bits);
    std::uint64_t profile_code = static_cast<std::uint64_t>(category.value());
    for (const auto group_bin : opponent_group_bins) {
      profile_code = (profile_code << 2U) | group_bin;
    }
    profile_code = (profile_code << 3U) | variance_bin;
    profile_code = (profile_code << 2U) | dominant_group;
    profile_code = (profile_code << 1U) | texture;
    auto profile_state = profile_code ^ partition_seed ^
                         (static_cast<std::uint64_t>(street) << 56U) ^ 0x5235'5052'4F46'5636ULL;
    const auto profile_mask = profile_bits == 0U ? 0U : (std::uint64_t{1U} << profile_bits) - 1U;
    const auto profile_bucket = splitmix64(profile_state) & profile_mask;
    return Result<std::uint16_t, HuPreflopError>::success(
        static_cast<std::uint16_t>((strength_bucket << profile_bits) | profile_bucket));
  }
  const auto hierarchical_bucket = feature_code >> (16U - capacity_bits);
  auto permutation_state =
      partition_seed ^ (static_cast<std::uint64_t>(street) << 48U) ^ 0x5235'4D41'5050'494EULL;
  const auto multiplier = splitmix64(permutation_state) | 1U;
  const auto offset = splitmix64(permutation_state);
  const auto bucket =
      (hierarchical_bucket * multiplier + offset) & (static_cast<std::uint64_t>(capacity) - 1U);
  return Result<std::uint16_t, HuPreflopError>::success(static_cast<std::uint16_t>(bucket));
}

HandClassId deal_class(const Deal &deal, const std::uint8_t player) {
  return hand_class(Combo{deal.holes[player][0], deal.holes[player][1]});
}

struct EvaluationSummary {
  double mean{0.0};
  double standard_error{0.0};
};

enum class SolverPolicyView : std::uint8_t { Average, Current };

struct SampledResponseSummary {
  EvaluationSummary co{};
  EvaluationSummary btn{};
  std::uint64_t information_sets{0U};
};

struct RootActionEvaluation {
  std::array<std::array<double, 5>, 81> means{};
  std::array<std::array<double, 5>, 81> standard_errors{};
  std::array<std::array<std::uint64_t, 5>, 81> samples{};
};

struct RootDecisionTraceMoments {
  std::uint64_t samples{0U};
  double total{0.0};
  double squared_total{0.0};

  void observe(const double payoff) noexcept {
    ++samples;
    total += payoff;
    squared_total += payoff * payoff;
  }

  HuPreflopRootDecisionTraceValueSummary
  summarize(const std::uint64_t action_samples) const noexcept {
    HuPreflopRootDecisionTraceValueSummary output;
    output.samples = samples;
    if (samples == 0U || action_samples == 0U) {
      return output;
    }
    const auto denominator = static_cast<double>(samples);
    output.probability = denominator / static_cast<double>(action_samples);
    output.mean_payoff_ante = total / denominator;
    if (samples > 1U) {
      const auto centered_sum = std::max(0.0, squared_total - total * total / denominator);
      output.standard_error_ante = std::sqrt(centered_sum / (denominator * (denominator - 1.0)));
    }
    output.ev_contribution_ante = total / static_cast<double>(action_samples);
    return output;
  }
};

struct RootDecisionTraceSample {
  std::vector<HuPreflopRootDecisionTraceStep> preflop_continuation;
  std::array<bool, 3> street_reached{};
  std::array<std::array<bool, 3>, 2> bucket_seen{};
  std::array<std::array<std::uint64_t, 3>, 2> bucket_keys{};
  HuPreflopTelemetryTerminalType terminal_type{
      HuPreflopTelemetryTerminalType::PostflopContinuation};
  Street terminal_street{Street::Preflop};
};

bool root_decision_trace_step_less(const HuPreflopRootDecisionTraceStep &left,
                                   const HuPreflopRootDecisionTraceStep &right) noexcept {
  if (left.node_id != right.node_id) {
    return left.node_id < right.node_id;
  }
  if (left.player != right.player) {
    return left.player < right.player;
  }
  return left.edge_index < right.edge_index;
}

struct RootDecisionTraceBranchKey {
  std::vector<HuPreflopRootDecisionTraceStep> preflop_continuation;
  HuPreflopTelemetryTerminalType terminal_type{
      HuPreflopTelemetryTerminalType::PostflopContinuation};
  Street terminal_street{Street::Preflop};

  friend bool operator<(const RootDecisionTraceBranchKey &left,
                        const RootDecisionTraceBranchKey &right) noexcept {
    if (left.preflop_continuation != right.preflop_continuation) {
      return std::lexicographical_compare(
          left.preflop_continuation.begin(), left.preflop_continuation.end(),
          right.preflop_continuation.begin(), right.preflop_continuation.end(),
          root_decision_trace_step_less);
    }
    if (left.terminal_type != right.terminal_type) {
      return static_cast<std::uint8_t>(left.terminal_type) <
             static_cast<std::uint8_t>(right.terminal_type);
    }
    return static_cast<std::uint8_t>(left.terminal_street) <
           static_cast<std::uint8_t>(right.terminal_street);
  }
};

struct RootDecisionTraceBucketKey {
  Street street{Street::Flop};
  std::uint8_t player{0U};
  std::uint64_t bucket_key{0U};

  friend bool operator<(const RootDecisionTraceBucketKey &left,
                        const RootDecisionTraceBucketKey &right) noexcept {
    if (left.street != right.street) {
      return static_cast<std::uint8_t>(left.street) < static_cast<std::uint8_t>(right.street);
    }
    if (left.player != right.player) {
      return left.player < right.player;
    }
    return left.bucket_key < right.bucket_key;
  }
};

ActionConfig sampled_postflop_action_config(const HuPreflopTree &tree) {
  ActionConfig config;
  config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                 tree.config.postflop_sizes.end());
  config.raise_depth = maximum_core_raise_depth;
  config.minimum_bet = tree.config.postflop_minimum_bet;
  config.all_in_mode = AllInMode::Add;
  config.all_in_threshold = PotPercentage::from_basis_points(100'000).value();
  return config;
}

struct CompiledBettingNode {
  std::uint64_t history{0U};
  PublicState state_template{};
  std::uint8_t action_count{0U};
  std::array<Action, maximum_actions> actions{};
  std::array<PublicState, maximum_actions> next_states{};
};

struct CompiledBettingPlan {
  std::vector<CompiledBettingNode> nodes;
  std::unordered_map<std::uint64_t, std::size_t> history_to_node;
};

std::uint64_t next_action_history(const std::uint64_t history, const Action &action) {
  return mix_history(history, static_cast<std::uint64_t>(action.amount.units()) ^
                                  (static_cast<std::uint64_t>(action.type) << 56U));
}

bool same_betting_state(PublicState left, PublicState right) {
  left.board_mask = 0U;
  right.board_mask = 0U;
  return left == right;
}

Result<PublicState, HuPreflopError> advance_compiled_street(const PublicState &state) {
  const auto advanced = advance_street(state);
  if (!advanced) {
    return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  auto next = advanced.value();
  if (next.street == Street::Flop) {
    next.board_mask = 0x7U;
  } else if (next.street == Street::Turn) {
    next.board_mask |= std::uint64_t{1U} << 3U;
  } else {
    next.board_mask |= std::uint64_t{1U} << 4U;
  }
  return validate_state(next)
             ? Result<PublicState, HuPreflopError>::success(next)
             : Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
}

Result<bool, HuPreflopError> compile_betting_subtree(const PublicState &state,
                                                     const std::uint64_t history,
                                                     const ActionConfig &config,
                                                     CompiledBettingPlan &plan) {
  if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
      state.status == HandStatus::Showdown) {
    return Result<bool, HuPreflopError>::success(true);
  }
  if (state.status == HandStatus::StreetComplete) {
    const auto next = advance_compiled_street(state);
    if (!next) {
      return Result<bool, HuPreflopError>::failure(next.error());
    }
    return compile_betting_subtree(
        next.value(),
        mix_history(history,
                    0x4348'414E'4345'0000ULL + static_cast<std::uint64_t>(next.value().street)),
        config, plan);
  }
  if (plan.history_to_node.contains(history)) {
    const auto &existing = plan.nodes[plan.history_to_node.at(history)];
    return same_betting_state(existing.state_template, state)
               ? Result<bool, HuPreflopError>::success(true)
               : Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto legal = legal_actions(state, config);
  if (!legal || legal.value().empty() || legal.value().size() > maximum_actions) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  CompiledBettingNode node;
  node.history = history;
  node.state_template = state;
  node.action_count = static_cast<std::uint8_t>(legal.value().size());
  for (std::size_t action = 0U; action < legal.value().size(); ++action) {
    node.actions[action] = legal.value()[action];
    const auto next = apply_action(state, node.actions[action], config);
    if (!next) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    node.next_states[action] = next.value();
  }
  const auto index = plan.nodes.size();
  plan.nodes.push_back(node);
  plan.history_to_node.emplace(history, index);
  for (std::size_t action = 0U; action < node.action_count; ++action) {
    const auto compiled = compile_betting_subtree(
        node.next_states[action], next_action_history(history, node.actions[action]), config, plan);
    if (!compiled) {
      return compiled;
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<CompiledBettingPlan, HuPreflopError> compile_betting_plan(const HuPreflopTree &tree,
                                                                 const ActionConfig &config) {
  try {
    CompiledBettingPlan plan;
    for (const auto &entry : tree.nodes) {
      if (entry.kind != HuPreflopNodeKind::PostflopEntry) {
        continue;
      }
      const auto flop = advance_compiled_street(entry.state);
      if (!flop) {
        return Result<CompiledBettingPlan, HuPreflopError>::failure(flop.error());
      }
      const auto history = mix_history(postflop_history_seed, entry.id);
      const auto compiled = compile_betting_subtree(flop.value(), history, config, plan);
      if (!compiled) {
        return Result<CompiledBettingPlan, HuPreflopError>::failure(compiled.error());
      }
    }
    return Result<CompiledBettingPlan, HuPreflopError>::success(std::move(plan));
  } catch (const std::bad_alloc &) {
    return Result<CompiledBettingPlan, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
  }
}

struct PostflopActionView {
  std::array<Action, maximum_actions> actions{};
  std::uint8_t action_count{0U};
  const CompiledBettingNode *compiled{nullptr};
};

struct PendingInformationUpdate {
  InformationKey key{};
  std::array<double, maximum_actions> regret_delta{};
  std::array<double, maximum_actions> strategy_sum_delta{};
  std::array<double, maximum_actions> root_action_advantage{};
  double opponent_baseline_observation{0.0};
  std::uint64_t iteration{0U};
  std::uint8_t action_count{0U};
  std::uint8_t opponent_baseline_action{std::numeric_limits<std::uint8_t>::max()};
  bool has_root_action_advantage{false};
};

struct RootActionAdvantageAccumulator {
  std::uint64_t observations{0U};
  double weight_sum{0.0};
  double squared_weight_sum{0.0};
  std::array<double, 5> weighted_sum{};
  std::array<double, 5> online_mean{};
  std::array<std::array<double, 5>, 5> co_moment{};
};

struct PendingTraversal {
  std::vector<PendingInformationUpdate> updates;
  Deal fixed_deal{};
  std::uint64_t random_seed{0U};
  std::uint64_t iteration{0U};
  double root_response_stratified_quantile{0.0};
  std::uint8_t traverser{0U};
  bool has_fixed_deal{false};
  bool first_traverser_decision_seen{false};
  HuPreflopError error{HuPreflopError::InvalidConfiguration};
};

struct ActionConditionedTelemetryKey {
  std::uint32_t node_id{0U};
  std::uint8_t player{0U};
  std::uint64_t history{0U};
  HandClassId hand_class{0U};
  std::uint8_t action_id{0U};
  std::uint64_t bucket_key{0U};
  Street postflop_street{Street::Preflop};
  HuPreflopTelemetryTerminalType terminal_type{
      HuPreflopTelemetryTerminalType::PostflopContinuation};

  friend bool operator==(const ActionConditionedTelemetryKey &,
                         const ActionConditionedTelemetryKey &) = default;

  friend bool operator<(const ActionConditionedTelemetryKey &left,
                        const ActionConditionedTelemetryKey &right) noexcept {
    if (left.node_id != right.node_id) {
      return left.node_id < right.node_id;
    }
    if (left.player != right.player) {
      return left.player < right.player;
    }
    if (left.history != right.history) {
      return left.history < right.history;
    }
    if (left.hand_class != right.hand_class) {
      return left.hand_class < right.hand_class;
    }
    if (left.action_id != right.action_id) {
      return left.action_id < right.action_id;
    }
    if (left.bucket_key != right.bucket_key) {
      return left.bucket_key < right.bucket_key;
    }
    if (left.postflop_street != right.postflop_street) {
      return static_cast<std::uint8_t>(left.postflop_street) <
             static_cast<std::uint8_t>(right.postflop_street);
    }
    return static_cast<std::uint8_t>(left.terminal_type) <
           static_cast<std::uint8_t>(right.terminal_type);
  }
};

struct ActionConditionedTelemetryAccumulator {
  std::uint64_t sample_count{0U};
  double physical_combo_mass{0.0};
  double public_reach_total{0.0};
  double own_reach_total{0.0};
  double action_value_mean{0.0};
  double action_value_m2{0.0};
  double action_advantage_total{0.0};
  double minimum_action_value{std::numeric_limits<double>::infinity()};
  double maximum_action_value{-std::numeric_limits<double>::infinity()};
  std::uint64_t bucket_occupancy{0U};
  std::uint64_t all_in_exact_count{0U};
  std::uint64_t all_in_sampled_count{0U};

  void observe(const double action_value, const double action_advantage, const double public_reach,
               const double own_reach,
               const HuPreflopTelemetryTerminalType terminal_type) noexcept {
    ++sample_count;
    physical_combo_mass += 1.0;
    public_reach_total += public_reach;
    own_reach_total += own_reach;
    const auto delta = action_value - action_value_mean;
    action_value_mean += delta / static_cast<double>(sample_count);
    action_value_m2 += delta * (action_value - action_value_mean);
    action_advantage_total += action_advantage;
    minimum_action_value = std::min(minimum_action_value, action_value);
    maximum_action_value = std::max(maximum_action_value, action_value);
    ++bucket_occupancy;
    if (terminal_type == HuPreflopTelemetryTerminalType::PreflopAllInExact ||
        terminal_type == HuPreflopTelemetryTerminalType::PostflopAllInExact) {
      ++all_in_exact_count;
    } else if (terminal_type == HuPreflopTelemetryTerminalType::PreflopAllInSampled ||
               terminal_type == HuPreflopTelemetryTerminalType::PostflopAllInSampled) {
      ++all_in_sampled_count;
    }
  }

  void merge(const ActionConditionedTelemetryAccumulator &other) noexcept {
    if (other.sample_count == 0U) {
      return;
    }
    if (sample_count == 0U) {
      *this = other;
      return;
    }
    const auto left_count = static_cast<double>(sample_count);
    const auto right_count = static_cast<double>(other.sample_count);
    const auto total_count = left_count + right_count;
    const auto mean_delta = other.action_value_mean - action_value_mean;
    action_value_m2 +=
        other.action_value_m2 + mean_delta * mean_delta * left_count * right_count / total_count;
    action_value_mean += mean_delta * right_count / total_count;
    sample_count += other.sample_count;
    physical_combo_mass += other.physical_combo_mass;
    public_reach_total += other.public_reach_total;
    own_reach_total += other.own_reach_total;
    action_advantage_total += other.action_advantage_total;
    minimum_action_value = std::min(minimum_action_value, other.minimum_action_value);
    maximum_action_value = std::max(maximum_action_value, other.maximum_action_value);
    bucket_occupancy += other.bucket_occupancy;
    all_in_exact_count += other.all_in_exact_count;
    all_in_sampled_count += other.all_in_sampled_count;
  }
};

struct ActionConditionedTelemetryKeyHash {
  std::size_t operator()(const ActionConditionedTelemetryKey &key) const noexcept {
    std::uint64_t value = key.node_id;
    value = mix_history(value, key.player);
    value = mix_history(value, key.history);
    value = mix_history(value, key.hand_class);
    value = mix_history(value, key.action_id);
    value = mix_history(value, key.bucket_key);
    value = mix_history(value, static_cast<std::uint64_t>(key.postflop_street));
    value = mix_history(value, static_cast<std::uint64_t>(key.terminal_type));
    return static_cast<std::size_t>(value);
  }
};

using ActionConditionedTelemetryMap =
    std::unordered_map<ActionConditionedTelemetryKey, ActionConditionedTelemetryAccumulator,
                       ActionConditionedTelemetryKeyHash>;

struct ParallelWorkerContext {
  std::array<std::array<BoundedBucketCache, 3>, 2> bucket_cache;
  BoundedExactAllInEquityCache exact_postflop_all_in_cache;
  std::array<std::uint64_t, 3> bucket_mapping_visits{};
  std::array<std::uint64_t, 3> bucket_mapping_computations{};
  std::array<std::bitset<hu_preflop_sampled_postflop_unset_bucket>, 3> occupied_buckets{};
  double bucket_mapping_seconds{0.0};
  std::uint64_t winner_cache_hits{0U};
  std::uint64_t winner_cache_misses{0U};
  std::array<std::uint64_t, 2> exact_postflop_all_in_evaluations{};
  std::array<std::uint64_t, 2> exact_postflop_all_in_runouts{};
  double exact_postflop_all_in_seconds{0.0};
  std::uint64_t exact_postflop_all_in_cache_hits{0U};
  std::uint64_t exact_postflop_all_in_cache_misses{0U};
  std::uint64_t action_value_spread_samples{0U};
  double action_value_spread_total{0.0};
  double action_value_spread_maximum{0.0};
  std::uint64_t peak_shadow_updates{0U};
  ActionConditionedTelemetryMap action_conditioned_telemetry;
  std::uint64_t action_conditioned_telemetry_dropped{0U};
  std::uint64_t action_conditioned_telemetry_entry_limit{0U};
};

class SampledSolver {
public:
  SampledSolver(const HuPreflopTree &tree, const HuPreflopSolveOptions &options)
      : tree_(tree), options_(options), random_(options.seed),
        blueprint_(options.iterations + options.preflop_refinement_iterations,
                   options.sampling_algorithm,
                   options.maximum_numeric_state_bytes /
                       (sizeof(InformationKey) + sizeof(InformationState))),
        opponent_value_baselines_(options.maximum_variance_baseline_bytes /
                                  (sizeof(InformationKey) + sizeof(OpponentValueBaselineState))),
        postflop_config_(sampled_postflop_action_config(tree)) {
    const auto cache_capacity_per_partition = options_.maximum_bucket_cache_entries / 6U;
    for (auto &player_caches : bucket_cache_) {
      for (auto &cache : player_caches) {
        cache.set_capacity(cache_capacity_per_partition);
      }
    }
    exact_postflop_all_in_cache_.set_capacity(options_.maximum_exact_postflop_all_in_cache_entries);
    if (!options_.seven_card_table_path.empty()) {
      auto loaded = SevenCardLookupTable::load(options_.seven_card_table_path);
      if (!loaded) {
        failure_ = loaded.error() == SevenCardTableError::MemoryFailure
                       ? HuPreflopError::MemoryFailure
                       : HuPreflopError::IntegrityFailure;
      } else {
        seven_card_table_.emplace(std::move(loaded.value()));
      }
    }
    if (options_.preflop_all_in_training_oracle &&
        !validate_hu_preflop_all_in_training_oracle(*options_.preflop_all_in_training_oracle)) {
      failure_ = HuPreflopError::IntegrityFailure;
    }
  }

  Result<HuPreflopSolveResult, HuPreflopError> solve() {
    if (failure_ != HuPreflopError::InvalidConfiguration) {
      return Result<HuPreflopSolveResult, HuPreflopError>::failure(failure_);
    }
    const auto started = std::chrono::steady_clock::now();
    if (options_.use_compiled_betting) {
      auto compiled = compile_betting_plan(tree_, postflop_config_);
      if (!compiled) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(compiled.error());
      }
      compiled_betting_.emplace(std::move(compiled.value()));
    }
    if (options_.sampling_algorithm == HuPreflopSamplingAlgorithm::ChanceSampledCfr) {
      for (std::uint64_t iteration = 1U; iteration <= options_.iterations; ++iteration) {
        auto deal = sample_deal(random_);
        const std::array<double, 2> reach{1.0, 1.0};
        (void)traverse_full_preflop(tree_.root, deal, 0U, iteration, reach);
        (void)traverse_full_preflop(tree_.root, deal, 1U, iteration, reach);
        if (failure_ != HuPreflopError::InvalidConfiguration || !blueprint_.valid()) {
          return Result<HuPreflopSolveResult, HuPreflopError>::failure(
              failure_ != HuPreflopError::InvalidConfiguration ? failure_ : blueprint_.error());
        }
      }
    } else if (options_.training_batch_iterations == 0U) {
      for (std::uint64_t iteration = 1U; iteration <= options_.iterations; ++iteration) {
        for (std::uint8_t traverser = 0; traverser < 2U; ++traverser) {
          auto deal = sample_deal(random_);
          const std::array<double, 2> reach{1.0, 1.0};
          (void)traverse_preflop(tree_.root, deal, blueprint_, nullptr, traverser, iteration, reach,
                                 false);
          if (failure_ != HuPreflopError::InvalidConfiguration || !blueprint_.valid()) {
            return Result<HuPreflopSolveResult, HuPreflopError>::failure(
                !blueprint_.valid() ? blueprint_.error() : failure_);
          }
        }
      }
    } else {
      const auto trained = train_parallel_batches();
      if (!trained) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(trained.error());
      }
    }
    const auto total_iterations = options_.iterations + options_.preflop_refinement_iterations;
    for (std::uint64_t iteration = options_.iterations + 1U; iteration <= total_iterations;
         ++iteration) {
      for (std::uint8_t traverser = 0U; traverser < 2U; ++traverser) {
        auto deal = sample_deal(random_);
        const std::array<double, 2> reach{1.0, 1.0};
        (void)traverse_preflop_refinement(tree_.root, deal, traverser, iteration, reach);
        if (failure_ != HuPreflopError::InvalidConfiguration || !blueprint_.valid()) {
          return Result<HuPreflopSolveResult, HuPreflopError>::failure(
              !blueprint_.valid() ? blueprint_.error() : failure_);
        }
      }
    }

    const auto baseline_query_start = blueprint_.average_policy_queries();
    const auto baseline_untrained_query_start = blueprint_.missing_average_policy_queries();
    const auto baseline =
        evaluate(blueprint_, blueprint_, options_.evaluation_deals, options_.evaluation_seed);
    if (!baseline) {
      return Result<HuPreflopSolveResult, HuPreflopError>::failure(baseline.error());
    }
    const auto baseline_average_policy_queries =
        blueprint_.average_policy_queries() - baseline_query_start;
    const auto baseline_untrained_average_policy_queries =
        blueprint_.missing_average_policy_queries() - baseline_untrained_query_start;

    const auto payload_bytes = sizeof(InformationKey) + sizeof(InformationState);
    const auto blueprint_bytes = blueprint_.size() * payload_bytes;
    const auto remaining_states =
        (options_.maximum_numeric_state_bytes - blueprint_bytes) / payload_bytes;
    const auto average_response =
        evaluate_sampled_response_profile(SolverPolicyView::Average, remaining_states, 0U);
    if (!average_response) {
      return Result<HuPreflopSolveResult, HuPreflopError>::failure(average_response.error());
    }
    const auto root_action_evaluation =
        evaluate_root_actions(blueprint_, blueprint_, options_.evaluation_deals,
                              options_.evaluation_seed ^ 0x524F'4F54'4143'5401ULL);
    if (!root_action_evaluation) {
      return Result<HuPreflopSolveResult, HuPreflopError>::failure(root_action_evaluation.error());
    }
    std::vector<HuPreflopDecisionEvaluation> preflop_decision_evaluations;
    if (options_.evaluate_preflop_decisions) {
      const auto evaluated = evaluate_preflop_decisions(
          blueprint_, blueprint_, options_.evaluation_deals,
          options_.evaluation_seed ^ 0x5052'4546'4556'0001ULL, root_action_evaluation.value());
      if (!evaluated) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(evaluated.error());
      }
      preflop_decision_evaluations = std::move(evaluated.value());
    }

    EvaluationSummary current_baseline;
    RootActionEvaluation current_root_action_evaluation;
    SampledResponseSummary current_response;
    std::vector<HuPreflopDecisionEvaluation> current_preflop_decision_evaluations;
    if (options_.evaluate_current_profile) {
      constexpr std::array current_views{SolverPolicyView::Current, SolverPolicyView::Current};
      const auto evaluated_current =
          evaluate(blueprint_, blueprint_, options_.evaluation_deals,
                   options_.evaluation_seed ^ 0x4355'5252'454E'5401ULL, current_views);
      if (!evaluated_current) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(evaluated_current.error());
      }
      current_baseline = evaluated_current.value();
      const auto current_actions =
          evaluate_root_actions(blueprint_, blueprint_, options_.evaluation_deals,
                                options_.evaluation_seed ^ 0x4355'5241'4354'0001ULL, current_views);
      if (!current_actions) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(current_actions.error());
      }
      current_root_action_evaluation = current_actions.value();
      const auto evaluated_response = evaluate_sampled_response_profile(
          SolverPolicyView::Current, remaining_states, 0x4355'5252'454E'5401ULL);
      if (!evaluated_response) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(evaluated_response.error());
      }
      current_response = evaluated_response.value();
      if (options_.evaluate_preflop_decisions) {
        const auto evaluated =
            evaluate_preflop_decisions(blueprint_, blueprint_, options_.evaluation_deals,
                                       options_.evaluation_seed ^ 0x4355'5052'4546'0001ULL,
                                       current_root_action_evaluation, current_views);
        if (!evaluated) {
          return Result<HuPreflopSolveResult, HuPreflopError>::failure(evaluated.error());
        }
        current_preflop_decision_evaluations = std::move(evaluated.value());
      }
    }

    std::vector<HuPreflopRootDecisionTrace> root_decision_traces;
    if (!options_.root_decision_trace_hand_classes.empty()) {
      const auto traced = evaluate_root_decision_traces();
      if (!traced) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(traced.error());
      }
      root_decision_traces = std::move(traced.value());
    }

    HuPreflopSolveResult result;
    result.iterations = total_iterations;
    result.postflop_training_iterations = options_.iterations;
    result.preflop_refinement_iterations = options_.preflop_refinement_iterations;
    result.seed = options_.seed;
    result.partition_seed = options_.partition_seed;
    result.evaluation_seed = options_.evaluation_seed;
    result.winner_cache_hits = winner_cache_hits_;
    result.winner_cache_misses = winner_cache_misses_;
    result.evaluator_backend_id =
        seven_card_table_.has_value()
            ? "seven_card_table_v1:" + std::to_string(seven_card_table_->checksum())
            : "exact_hand_evaluator_oracle_v1";
    result.evaluator_table_payload_bytes =
        seven_card_table_.has_value() ? seven_card_table_->payload_bytes() : 0U;
    std::uint64_t main_bucket_cache_peak = 0U;
    for (const auto &player_caches : bucket_cache_) {
      for (const auto &cache : player_caches) {
        main_bucket_cache_peak += cache.peak_entries();
        result.bucket_cache_evictions += cache.evictions();
      }
    }
    result.bucket_cache_peak_entries =
        std::max(main_bucket_cache_peak, parallel_bucket_cache_peak_entries_);
    result.bucket_cache_evictions += bucket_cache_evictions_from_parallel_;
    result.root_ev_ante = baseline.value().mean;
    result.root_ev_standard_error_ante = baseline.value().standard_error;
    result.root_action_ev_ante = root_action_evaluation.value().means;
    result.root_action_ev_standard_error_ante = root_action_evaluation.value().standard_errors;
    result.root_action_ev_samples = root_action_evaluation.value().samples;
    result.best_response_co_ev_ante = average_response.value().co.mean;
    result.best_response_btn_ev_ante = average_response.value().btn.mean;
    result.best_response_co_standard_error_ante = average_response.value().co.standard_error;
    result.best_response_btn_standard_error_ante = average_response.value().btn.standard_error;
    result.abstract_nashconv_ante =
        std::max(0.0, result.best_response_co_ev_ante + result.best_response_btn_ev_ante);
    const auto effective_stack_ante = static_cast<double>(tree_.config.effective_stack.units()) /
                                      static_cast<double>(Money::units_per_ante);
    result.normalized_abstract_nashconv = result.abstract_nashconv_ante / effective_stack_ante;
    result.sampled_response_lower_bound_ante =
        std::max(0.0, result.best_response_co_ev_ante - result.root_ev_ante) +
        std::max(0.0, result.best_response_btn_ev_ante + result.root_ev_ante);
    result.normalized_sampled_response_lower_bound =
        result.sampled_response_lower_bound_ante / effective_stack_ante;
    if (options_.evaluate_current_profile) {
      result.current_profile_evaluated = true;
      result.current_profile_root_ev_ante = current_baseline.mean;
      result.current_profile_root_ev_standard_error_ante = current_baseline.standard_error;
      result.current_profile_root_action_ev_ante.assign(
          current_root_action_evaluation.means.begin(), current_root_action_evaluation.means.end());
      result.current_profile_root_action_ev_standard_error_ante.assign(
          current_root_action_evaluation.standard_errors.begin(),
          current_root_action_evaluation.standard_errors.end());
      result.current_profile_root_action_ev_samples.assign(
          current_root_action_evaluation.samples.begin(),
          current_root_action_evaluation.samples.end());
      result.current_profile_best_response_co_ev_ante = current_response.co.mean;
      result.current_profile_best_response_btn_ev_ante = current_response.btn.mean;
      result.current_profile_best_response_co_standard_error_ante =
          current_response.co.standard_error;
      result.current_profile_best_response_btn_standard_error_ante =
          current_response.btn.standard_error;
      result.current_profile_sampled_response_lower_bound_ante =
          std::max(0.0, result.current_profile_best_response_co_ev_ante -
                            result.current_profile_root_ev_ante) +
          std::max(0.0, result.current_profile_best_response_btn_ev_ante +
                            result.current_profile_root_ev_ante);
      result.current_profile_normalized_sampled_response_lower_bound =
          result.current_profile_sampled_response_lower_bound_ante / effective_stack_ante;
    }
    result.information_sets = blueprint_.size();
    result.best_response_information_sets = average_response.value().information_sets;
    result.current_profile_best_response_information_sets = current_response.information_sets;
    result.bytes_per_information_set_payload = sizeof(InformationKey) + sizeof(InformationState);
    if (result.information_sets >
            std::numeric_limits<std::uint64_t>::max() / result.bytes_per_information_set_payload ||
        std::max(result.best_response_information_sets,
                 result.current_profile_best_response_information_sets) >
            std::numeric_limits<std::uint64_t>::max() / result.bytes_per_information_set_payload) {
      return Result<HuPreflopSolveResult, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    result.minimum_blueprint_payload_bytes =
        result.information_sets * result.bytes_per_information_set_payload;
    result.minimum_best_response_payload_bytes =
        std::max(result.best_response_information_sets,
                 result.current_profile_best_response_information_sets) *
        result.bytes_per_information_set_payload;
    result.numeric_state_payload_bytes =
        result.minimum_blueprint_payload_bytes + result.minimum_best_response_payload_bytes;
    result.numeric_state_budget_bytes = options_.maximum_numeric_state_bytes;
    result.bucket_mapping_visits = bucket_mapping_visits_;
    result.bucket_mapping_computations = bucket_mapping_computations_;
    result.distributional_bucket_capacities = options_.distributional_bucket_capacities;
    result.average_policy_queries = baseline_average_policy_queries;
    result.untrained_average_policy_queries = baseline_untrained_average_policy_queries;
    result.bucket_mapping_seconds = bucket_mapping_seconds_;
    result.postflop_action_value_spread_samples = postflop_action_value_spread_samples_;
    result.mean_postflop_action_value_spread_ante =
        postflop_action_value_spread_samples_ == 0U
            ? 0.0
            : postflop_action_value_spread_total_ /
                  static_cast<double>(postflop_action_value_spread_samples_);
    result.maximum_postflop_action_value_spread_ante = postflop_action_value_spread_maximum_;
    result.worker_threads = options_.worker_threads;
    result.training_batch_iterations = options_.training_batch_iterations;
    result.root_action_value_rollouts = options_.root_action_value_rollouts;
    result.root_continuation_mean_updates = options_.root_continuation_mean_updates;
    result.symmetric_traverser_mean_updates = options_.symmetric_traverser_mean_updates;
    result.root_common_random_numbers = options_.root_common_random_numbers;
    result.global_common_random_numbers = options_.global_common_random_numbers;
    result.root_first_opponent_response_stratification =
        options_.root_first_opponent_response_stratification;
    result.peak_parallel_updates_per_job = peak_parallel_updates_per_job_;
    result.peak_parallel_shadow_updates_per_worker = peak_parallel_shadow_updates_per_worker_;
    result.peak_parallel_scratch_payload_bytes = peak_parallel_scratch_payload_bytes_;
    result.variance_baseline_information_sets = opponent_value_baselines_.size();
    result.variance_baseline_payload_bytes = opponent_value_baselines_.payload_bytes();
    result.exact_preflop_all_in_expectation =
        static_cast<bool>(options_.preflop_all_in_training_oracle);
    if (options_.preflop_all_in_training_oracle) {
      result.preflop_all_in_equity_table_fingerprint =
          options_.preflop_all_in_training_oracle->source_equity_table_fingerprint;
    }
    result.postflop_all_in_expectation_id =
        postflop_all_in_expectation_id(options_.postflop_all_in_expectation_mode);
    result.exact_postflop_all_in_evaluations = exact_postflop_all_in_evaluations_;
    result.exact_postflop_all_in_runouts = exact_postflop_all_in_runouts_;
    result.exact_postflop_all_in_seconds = exact_postflop_all_in_seconds_;
    result.exact_postflop_all_in_cache_hits = exact_postflop_all_in_cache_hits_;
    result.exact_postflop_all_in_cache_misses = exact_postflop_all_in_cache_misses_;
    result.exact_postflop_all_in_cache_peak_entries =
        std::max(parallel_exact_postflop_all_in_cache_peak_entries_,
                 exact_postflop_all_in_cache_.peak_entries());
    result.exact_postflop_all_in_cache_evictions =
        parallel_exact_postflop_all_in_cache_evictions_ + exact_postflop_all_in_cache_.evictions();
    result.chance_sampling_id = chance_sampling_id(options_.chance_sampling_mode);
    for (std::size_t street = 0U; street < occupied_distributional_buckets_.size(); ++street) {
      result.occupied_distributional_buckets[street] =
          occupied_distributional_buckets_[street].count();
    }
    if (compiled_betting_.has_value()) {
      result.compiled_betting_nodes = compiled_betting_->nodes.size();
      result.compiled_betting_bytes =
          compiled_betting_->nodes.size() * sizeof(CompiledBettingNode) +
          compiled_betting_->history_to_node.size() * (sizeof(std::uint64_t) + sizeof(std::size_t));
    }
    result.tree_fingerprint = tree_.fingerprint;
    result.abstraction_id = postflop_abstraction_id(options_);
    if (options_.postflop_representation ==
        HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23) {
      const auto abstract_game = make_hu_preflop_abstract_game_definition(tree_, options_);
      if (!abstract_game) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(abstract_game.error());
      }
      result.abstract_game_fingerprint = abstract_game.value().fingerprint;
    }
    result.algorithm_id = sampled_algorithm_id(options_.sampling_algorithm);
    if (options_.training_batch_iterations != 0U) {
      result.algorithm_id += "_deterministic_batch" +
                             std::to_string(options_.training_batch_iterations) + "_workers" +
                             std::to_string(options_.worker_threads);
    }
    if (options_.root_action_value_rollouts > 1U) {
      result.algorithm_id +=
          "_root_action_rollouts" + std::to_string(options_.root_action_value_rollouts) + "_v1";
    }
    if (options_.root_continuation_mean_updates) {
      result.algorithm_id += "_continuation_mean_updates_v1";
    }
    if (options_.symmetric_traverser_mean_updates) {
      result.algorithm_id += "_symmetric_traverser_mean_updates_v1";
    }
    if (options_.root_common_random_numbers) {
      result.algorithm_id += "_root_common_random_numbers_v1";
    }
    if (options_.global_common_random_numbers) {
      result.algorithm_id += "_global_common_random_numbers_v1";
    }
    if (options_.root_first_opponent_response_stratification) {
      result.algorithm_id += "_root_first_opponent_response_stratification_v1";
    }
    if (options_.evaluate_current_profile) {
      result.algorithm_id += "_full_current_profile_evaluation_v1";
    }
    if (options_.preflop_all_in_training_oracle) {
      result.algorithm_id += "_exact_preflop_all_in_expectation_v1";
    }
    if (options_.postflop_all_in_expectation_mode ==
        HuPreflopPostflopAllInExpectationMode::ExactTurn) {
      result.algorithm_id += "_exact_postflop_all_in_turn_v1";
    } else if (options_.postflop_all_in_expectation_mode ==
               HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn) {
      result.algorithm_id += "_exact_postflop_all_in_flop_turn_v1";
    }
    if (options_.use_opponent_value_baseline) {
      result.algorithm_id += "_opponent_action_baseline_v1";
    }
    if (options_.chance_sampling_mode == HuPreflopChanceSamplingMode::PublicBoardStratified) {
      result.algorithm_id += "_public_board_stratified_v1";
    }
    if (options_.preflop_refinement_iterations != 0U) {
      result.algorithm_id += "_frozen_postflop_rollout_refinement_v1";
    }
    extract_root_strategy(result.root_strategy);
    extract_root_training_diagnostics(result);
    for (auto &trace : root_decision_traces) {
      const auto hand = static_cast<std::size_t>(trace.hand_class);
      trace.average_strategy = result.root_strategy[hand];
      trace.current_strategy = result.root_current_strategy[hand];
      trace.cumulative_weighted_regret = result.root_cumulative_weighted_regret[hand];
      trace.cumulative_average_weight = result.root_cumulative_average_weight[hand];
      trace.last_update_iteration = result.root_information_last_iteration[hand];
      trace.training_action_advantage = result.root_action_advantage_diagnostics[hand];
    }
    result.root_decision_traces = std::move(root_decision_traces);
    extract_preflop_blueprint(result.preflop_blueprint, result.algorithm_id);
    result.preflop_decision_evaluations = std::move(preflop_decision_evaluations);
    result.current_profile_preflop_decision_evaluations =
        std::move(current_preflop_decision_evaluations);
    if (options_.evaluate_preflop_decisions) {
      extract_preflop_training_diagnostics(result.preflop_decision_training_diagnostics);
    }
    result.action_conditioned_telemetry_enabled = options_.collect_action_conditioned_telemetry;
    result.action_conditioned_telemetry_dropped = action_conditioned_telemetry_dropped_;
    if (result.action_conditioned_telemetry_enabled) {
      extract_action_conditioned_telemetry(result.action_conditioned_telemetry);
      action_conditioned_telemetry_.clear();
      action_conditioned_telemetry_.rehash(0U);
    }
    if (options_.export_postflop_policy) {
      const auto exported = extract_postflop_policy(result.postflop_policy, result.algorithm_id,
                                                    result.abstraction_id, total_iterations);
      if (!exported) {
        return Result<HuPreflopSolveResult, HuPreflopError>::failure(exported.error());
      }
      result.minimum_exported_postflop_policy_payload_bytes = exported.value();
    }
    result.solve_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return Result<HuPreflopSolveResult, HuPreflopError>::success(std::move(result));
  }

private:
  bool append_pending_update(PendingTraversal &pending, const InformationKey &key,
                             const std::size_t action_count, const std::uint64_t iteration,
                             const std::array<double, maximum_actions> &regret_delta,
                             const std::array<double, maximum_actions> &strategy_sum_delta) const {
    if (pending.updates.size() >= options_.maximum_parallel_updates_per_job) {
      pending.error = HuPreflopError::MemoryFailure;
      return false;
    }
    try {
      PendingInformationUpdate update;
      update.key = key;
      update.regret_delta = regret_delta;
      update.strategy_sum_delta = strategy_sum_delta;
      update.iteration = iteration;
      update.action_count = static_cast<std::uint8_t>(action_count);
      pending.updates.push_back(std::move(update));
      return true;
    } catch (const std::bad_alloc &) {
      pending.error = HuPreflopError::MemoryFailure;
      return false;
    }
  }

  void scale_pending_updates(PendingTraversal &pending, const std::size_t begin,
                             const double scale) const {
    for (std::size_t index = begin; index < pending.updates.size(); ++index) {
      auto &update = pending.updates[index];
      for (std::size_t action = 0U; action < update.action_count; ++action) {
        update.regret_delta[action] *= scale;
        update.strategy_sum_delta[action] *= scale;
      }
    }
  }

  bool append_scaled_pending_updates(PendingTraversal &target, PendingTraversal &source,
                                     const double scale) const {
    if (source.updates.size() > options_.maximum_parallel_updates_per_job - target.updates.size()) {
      target.error = HuPreflopError::MemoryFailure;
      return false;
    }
    try {
      for (auto &update : source.updates) {
        for (std::size_t action = 0U; action < update.action_count; ++action) {
          update.regret_delta[action] *= scale;
          update.strategy_sum_delta[action] *= scale;
        }
        target.updates.push_back(std::move(update));
      }
      return true;
    } catch (const std::bad_alloc &) {
      target.error = HuPreflopError::MemoryFailure;
      return false;
    }
  }

  Result<InformationKey, HuPreflopError>
  parallel_postflop_key(const PublicState &state, Deal &deal, const std::uint64_t history,
                        ParallelWorkerContext &context) const {
    InformationKey key;
    key.public_history = history;
    key.player = state.player_to_act;
    key.street = state.street;
    const bool forgets_preflop_class =
        representation_forgets_preflop_class(options_.postflop_representation);
    key.preflop_class =
        forgets_preflop_class ? HandClassId{0U} : deal_class(deal, state.player_to_act);
    if (options_.postflop_representation == HuPreflopPostflopRepresentation::ExactPhysical) {
      key.physical_cards = physical_cards_key(deal, state.player_to_act, state.street);
      return Result<InformationKey, HuPreflopError>::success(key);
    }
    const auto final_index =
        static_cast<std::size_t>(state.street) - static_cast<std::size_t>(Street::Flop);
    const bool current_street_only =
        representation_uses_current_street_only(options_.postflop_representation);
    const auto first_index =
        uses_one_street_history(options_.postflop_representation) && final_index > 0U
            ? final_index - 1U
        : current_street_only ? final_index
                              : 0U;
    for (std::size_t index = first_index; index <= final_index; ++index) {
      ++context.bucket_mapping_visits[index];
      const auto bucket_street =
          static_cast<Street>(static_cast<std::size_t>(Street::Flop) + index);
      auto &bucket = deal.buckets[state.player_to_act][index];
      if (bucket == unset_bucket) {
        const auto observation_key = physical_cards_key(deal, state.player_to_act, bucket_street);
        auto &cache = context.bucket_cache[state.player_to_act][index];
        const auto cached = cache.find(observation_key);
        if (cached.has_value()) {
          bucket = cached.value();
        } else {
          const auto mapping_started = std::chrono::steady_clock::now();
          const auto computed =
              is_distributional_strength_representation(options_.postflop_representation)
                  ? compute_distributional_strength_bucket(
                        deal, state.player_to_act, bucket_street,
                        options_.equity_samples_per_bucket, options_.partition_seed,
                        options_.distributional_bucket_capacities,
                        uses_distributional_profile_v6(options_.postflop_representation),
                        uses_distributional_structured_v7(options_.postflop_representation),
                        uses_distributional_street_adaptive_v8(options_.postflop_representation))
                  : compute_bucket(deal, state.player_to_act, bucket_street,
                                   options_.equity_samples_per_bucket, options_.partition_seed);
          if (!computed) {
            return Result<InformationKey, HuPreflopError>::failure(computed.error());
          }
          context.bucket_mapping_seconds +=
              std::chrono::duration<double>(std::chrono::steady_clock::now() - mapping_started)
                  .count();
          ++context.bucket_mapping_computations[index];
          bucket = computed.value();
          cache.insert(observation_key, bucket);
        }
      }
      if (is_distributional_strength_representation(options_.postflop_representation)) {
        context.occupied_buckets[index].set(bucket);
      }
      const auto information_bucket = distributional_information_bucket(
          deal, state.player_to_act, bucket_street, index, final_index, bucket,
          options_.distributional_bucket_capacities[index], options_.postflop_representation);
      if (!information_bucket) {
        return Result<InformationKey, HuPreflopError>::failure(information_bucket.error());
      }
      key.bucket_history[index] = information_bucket.value();
    }
    return Result<InformationKey, HuPreflopError>::success(key);
  }

  static std::uint64_t telemetry_bucket_key(const InformationKey &key) noexcept {
    std::uint64_t value = key.physical_cards;
    for (const auto bucket : key.bucket_history) {
      value = mix_history(value, bucket);
    }
    value = mix_history(value, static_cast<std::uint64_t>(key.street));
    value = mix_history(value, static_cast<std::uint64_t>(key.player));
    value = mix_history(value, static_cast<std::uint64_t>(key.preflop_class));
    return value;
  }

  HuPreflopTelemetryTerminalType
  preflop_telemetry_terminal(const HuPreflopNode &child) const noexcept {
    if (child.kind == HuPreflopNodeKind::TerminalFold) {
      return HuPreflopTelemetryTerminalType::PreflopFold;
    }
    if (child.kind == HuPreflopNodeKind::TerminalAllIn) {
      return options_.preflop_all_in_training_oracle
                 ? HuPreflopTelemetryTerminalType::PreflopAllInExact
                 : HuPreflopTelemetryTerminalType::PreflopAllInSampled;
    }
    return HuPreflopTelemetryTerminalType::PostflopContinuation;
  }

  HuPreflopTelemetryTerminalType
  postflop_telemetry_terminal(const PublicState &state, const PublicState &next) const noexcept {
    if (next.status == HandStatus::Folded) {
      return HuPreflopTelemetryTerminalType::PostflopFold;
    }
    if (next.status == HandStatus::AllInRunout) {
      const auto exact = should_integrate_postflop_all_in(next);
      if (exact) {
        return HuPreflopTelemetryTerminalType::PostflopAllInExact;
      }
      return HuPreflopTelemetryTerminalType::PostflopAllInSampled;
    }
    if (next.status == HandStatus::Showdown) {
      return HuPreflopTelemetryTerminalType::PostflopShowdown;
    }
    static_cast<void>(state);
    return HuPreflopTelemetryTerminalType::PostflopContinuation;
  }

  void observe_action_conditioned_telemetry(ActionConditionedTelemetryMap &target,
                                            const std::uint32_t node_id, const InformationKey &key,
                                            const PublicState &state, const std::uint8_t action_id,
                                            const HuPreflopTelemetryTerminalType terminal_type,
                                            const Deal &deal, const std::array<double, 2> &reach,
                                            const double action_value, const double node_value,
                                            std::uint64_t &dropped,
                                            const std::uint64_t maximum_entries) const {
    if (!options_.collect_action_conditioned_telemetry) {
      return;
    }
    if (action_id >= maximum_actions || !std::isfinite(action_value) ||
        !std::isfinite(node_value) || !std::isfinite(reach[0]) || !std::isfinite(reach[1])) {
      const_cast<SampledSolver *>(this)->failure_ = HuPreflopError::NumericalFailure;
      return;
    }
    try {
      const ActionConditionedTelemetryKey telemetry_key{node_id,
                                                        state.player_to_act,
                                                        key.public_history,
                                                        deal_class(deal, state.player_to_act),
                                                        action_id,
                                                        telemetry_bucket_key(key),
                                                        state.street,
                                                        terminal_type};
      auto entry = target.find(telemetry_key);
      if (entry == target.end() && target.size() >= maximum_entries) {
        ++dropped;
        return;
      }
      auto &accumulator =
          entry == target.end()
              ? target.emplace(telemetry_key, ActionConditionedTelemetryAccumulator{}).first->second
              : entry->second;
      accumulator.observe(action_value, action_value - node_value, reach[0] * reach[1],
                          reach[state.player_to_act], terminal_type);
    } catch (const std::bad_alloc &) {
      const_cast<SampledSolver *>(this)->failure_ = HuPreflopError::MemoryFailure;
    }
  }

  void extract_action_conditioned_telemetry(
      std::vector<HuPreflopActionConditionedTelemetry> &output) const {
    output.reserve(action_conditioned_telemetry_.size());
    for (const auto &[key, accumulator] : action_conditioned_telemetry_) {
      if (accumulator.sample_count == 0U) {
        continue;
      }
      HuPreflopActionConditionedTelemetry row;
      row.node_id = key.node_id;
      row.player = key.player;
      row.history = key.history;
      row.hand_class = key.hand_class;
      row.physical_combo_mass = accumulator.physical_combo_mass;
      row.public_reach =
          accumulator.public_reach_total / static_cast<double>(accumulator.sample_count);
      row.own_reach = accumulator.own_reach_total / static_cast<double>(accumulator.sample_count);
      row.action_id = key.action_id;
      row.sample_count = accumulator.sample_count;
      row.mean_action_value = accumulator.action_value_mean;
      row.variance_action_value =
          accumulator.sample_count > 1U
              ? std::max(0.0, accumulator.action_value_m2 /
                                  static_cast<double>(accumulator.sample_count - 1U))
              : 0.0;
      row.standard_error_action_value =
          std::sqrt(row.variance_action_value / static_cast<double>(row.sample_count));
      row.mean_action_advantage =
          accumulator.action_advantage_total / static_cast<double>(accumulator.sample_count);
      row.bucket_key = key.bucket_key;
      row.bucket_occupancy = accumulator.bucket_occupancy;
      row.postflop_street = key.postflop_street;
      row.terminal_type = key.terminal_type;
      row.all_in_exact_count = accumulator.all_in_exact_count;
      row.all_in_sampled_count = accumulator.all_in_sampled_count;
      row.minimum_action_value =
          std::isfinite(accumulator.minimum_action_value) ? accumulator.minimum_action_value : 0.0;
      row.maximum_action_value =
          std::isfinite(accumulator.maximum_action_value) ? accumulator.maximum_action_value : 0.0;
      row.spread_action_value = row.maximum_action_value - row.minimum_action_value;
      output.push_back(row);
    }
    std::sort(output.begin(), output.end(), [](const auto &left, const auto &right) {
      if (left.node_id != right.node_id) {
        return left.node_id < right.node_id;
      }
      if (left.player != right.player) {
        return left.player < right.player;
      }
      if (left.history != right.history) {
        return left.history < right.history;
      }
      if (left.hand_class != right.hand_class) {
        return left.hand_class < right.hand_class;
      }
      if (left.action_id != right.action_id) {
        return left.action_id < right.action_id;
      }
      if (left.bucket_key != right.bucket_key) {
        return left.bucket_key < right.bucket_key;
      }
      if (left.postflop_street != right.postflop_street) {
        return static_cast<std::uint8_t>(left.postflop_street) <
               static_cast<std::uint8_t>(right.postflop_street);
      }
      return static_cast<std::uint8_t>(left.terminal_type) <
             static_cast<std::uint8_t>(right.terminal_type);
    });
  }

  Result<double, HuPreflopError> parallel_terminal_payoff(const PublicState &state, Deal &deal,
                                                          const std::uint8_t player,
                                                          ParallelWorkerContext &context) const {
    if (should_integrate_postflop_all_in(state)) {
      const auto started = std::chrono::steady_clock::now();
      const auto expected = exact_postflop_all_in_payoff(
          state, deal, player, context.exact_postflop_all_in_cache,
          context.exact_postflop_all_in_cache_hits, context.exact_postflop_all_in_cache_misses);
      context.exact_postflop_all_in_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      if (!expected) {
        return Result<double, HuPreflopError>::failure(expected.error());
      }
      const auto street_index = state.street == Street::Flop ? 0U : 1U;
      ++context.exact_postflop_all_in_evaluations[street_index];
      context.exact_postflop_all_in_runouts[street_index] += expected.value().second;
      return Result<double, HuPreflopError>::success(expected.value().first);
    }
    std::uint8_t winner_mask = 0U;
    if (state.status != HandStatus::Folded) {
      if (deal.winner_mask < 0) {
        const std::vector<std::array<CardId, 2>> holes{deal.holes[0], deal.holes[1]};
        const std::vector<CardId> board(deal.board.begin(), deal.board.end());
        const auto showdown = seven_card_table_.has_value()
                                  ? evaluate_showdown(holes, board, *seven_card_table_)
                                  : evaluate_showdown(holes, board);
        if (!showdown) {
          return Result<double, HuPreflopError>::failure(HuPreflopError::EquityFailure);
        }
        deal.winner_mask = static_cast<std::int8_t>(showdown.value().winner_mask);
        ++context.winner_cache_misses;
      } else {
        ++context.winner_cache_hits;
      }
      winner_mask = static_cast<std::uint8_t>(deal.winner_mask);
    }
    const auto settlement = settle_terminal(state, tree_.config.rake, winner_mask);
    return settlement ? Result<double, HuPreflopError>::success(
                            static_cast<double>(settlement.value().payoff_units[player]) /
                            static_cast<double>(Money::units_per_ante))
                      : Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }

  Result<double, HuPreflopError> exact_preflop_all_in_payoff(const PublicState &state,
                                                             const Deal &deal,
                                                             const std::uint8_t player) const {
    if (!options_.preflop_all_in_training_oracle || player > 1U) {
      return Result<double, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    const auto player_class = deal_class(deal, player);
    const auto opponent_class = deal_class(deal, static_cast<std::uint8_t>(1U - player));
    const auto index = static_cast<std::size_t>(player_class) * hu_preflop_hand_class_count +
                       static_cast<std::size_t>(opponent_class);
    const auto &equity = options_.preflop_all_in_training_oracle->matchups[index];
    const auto loss_probability =
        std::max(0.0, 1.0 - equity.win_probability - equity.tie_probability);
    const auto player_mask = static_cast<std::uint8_t>(1U << player);
    const auto opponent_mask = static_cast<std::uint8_t>(1U << (1U - player));
    const auto win = settle_terminal(state, tree_.config.rake, player_mask);
    const auto tie = settle_terminal(state, tree_.config.rake, 0b11U);
    const auto loss = settle_terminal(state, tree_.config.rake, opponent_mask);
    if (!win || !tie || !loss) {
      return Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    const auto expected_units =
        equity.win_probability * static_cast<double>(win.value().payoff_units[player]) +
        equity.tie_probability * static_cast<double>(tie.value().payoff_units[player]) +
        loss_probability * static_cast<double>(loss.value().payoff_units[player]);
    const auto expected_antes = expected_units / static_cast<double>(Money::units_per_ante);
    return std::isfinite(expected_antes)
               ? Result<double, HuPreflopError>::success(expected_antes)
               : Result<double, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
  }

  [[nodiscard]] bool should_integrate_postflop_all_in(const PublicState &state) const noexcept {
    if (state.status != HandStatus::AllInRunout) {
      return false;
    }
    if (state.street == Street::Turn) {
      return options_.postflop_all_in_expectation_mode ==
                 HuPreflopPostflopAllInExpectationMode::ExactTurn ||
             options_.postflop_all_in_expectation_mode ==
                 HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn;
    }
    return state.street == Street::Flop &&
           options_.postflop_all_in_expectation_mode ==
               HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn;
  }

  [[nodiscard]] const IHandEvaluator &all_in_evaluator() const noexcept {
    static const ExactHandEvaluator fallback;
    return seven_card_table_.has_value() ? static_cast<const IHandEvaluator &>(*seven_card_table_)
                                         : static_cast<const IHandEvaluator &>(fallback);
  }

  Result<std::pair<double, std::uint64_t>, HuPreflopError>
  exact_postflop_all_in_payoff(const PublicState &state, const Deal &deal,
                               const std::uint8_t player, BoundedExactAllInEquityCache &cache,
                               std::uint64_t &cache_hits, std::uint64_t &cache_misses) const {
    if (player > 1U || !should_integrate_postflop_all_in(state)) {
      return Result<std::pair<double, std::uint64_t>, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
    const auto cache_key = canonical_exact_all_in_equity_key(deal, state.street);
    HuPreflopPostflopAllInEquity equity;
    if (const auto cached = cache.find(cache_key); cached.has_value()) {
      equity = *cached;
      ++cache_hits;
    } else {
      const auto enumerated = enumerate_hu_preflop_postflop_all_in_equity(
          deal.holes, deal.board, state.street, all_in_evaluator());
      if (!enumerated || enumerated.value().runouts() == 0U) {
        return Result<std::pair<double, std::uint64_t>, HuPreflopError>::failure(
            enumerated ? HuPreflopError::IntegrityFailure : enumerated.error());
      }
      equity = enumerated.value();
      cache.insert(cache_key, equity);
      ++cache_misses;
    }
    const auto player_mask = static_cast<std::uint8_t>(1U << player);
    const auto opponent_mask = static_cast<std::uint8_t>(1U << (1U - player));
    const auto win = settle_terminal(state, tree_.config.rake, player_mask);
    const auto tie = settle_terminal(state, tree_.config.rake, 0b11U);
    const auto loss = settle_terminal(state, tree_.config.rake, opponent_mask);
    if (!win || !tie || !loss) {
      return Result<std::pair<double, std::uint64_t>, HuPreflopError>::failure(
          HuPreflopError::GameFailure);
    }
    const auto win_count = player == 0U ? equity.wins : equity.losses;
    const auto loss_count = player == 0U ? equity.losses : equity.wins;
    const auto expected_units =
        (static_cast<double>(win_count) * static_cast<double>(win.value().payoff_units[player]) +
         static_cast<double>(equity.ties) * static_cast<double>(tie.value().payoff_units[player]) +
         static_cast<double>(loss_count) * static_cast<double>(loss.value().payoff_units[player])) /
        static_cast<double>(equity.runouts());
    const auto expected_antes = expected_units / static_cast<double>(Money::units_per_ante);
    return std::isfinite(expected_antes)
               ? Result<std::pair<double, std::uint64_t>, HuPreflopError>::success(
                     {expected_antes, equity.runouts()})
               : Result<std::pair<double, std::uint64_t>, HuPreflopError>::failure(
                     HuPreflopError::NumericalFailure);
  }

  Result<double, HuPreflopError> collect_parallel_postflop(
      const PublicState &state, Deal &deal, const std::uint64_t history,
      const std::uint32_t entry_node_id, const std::uint8_t traverser,
      const std::uint64_t iteration, const std::array<double, 2> &reach, std::mt19937_64 &random,
      ParallelWorkerContext &context, PendingTraversal &pending,
      const std::optional<double> forced_opponent_response_quantile = std::nullopt) const {
    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
        state.status == HandStatus::Showdown) {
      return parallel_terminal_payoff(state, deal, traverser, context);
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_with_board(state, deal);
      if (!advanced) {
        return Result<double, HuPreflopError>::failure(advanced.error());
      }
      return collect_parallel_postflop(
          advanced.value(), deal,
          mix_history(history, 0x4348'414E'4345'0000ULL +
                                   static_cast<std::uint64_t>(advanced.value().street)),
          entry_node_id, traverser, iteration, reach, random, context, pending,
          forced_opponent_response_quantile);
    }
    const auto actions = postflop_actions(state, history);
    const auto key = parallel_postflop_key(state, deal, history, context);
    if (!actions || !key) {
      return Result<double, HuPreflopError>::failure(!actions ? actions.error() : key.error());
    }
    const auto action_count = actions.value().action_count;
    const auto strategy = blueprint_.frozen_current_strategy(key.value(), action_count);
    if (!strategy) {
      return Result<double, HuPreflopError>::failure(strategy.error());
    }
    const auto actor = state.player_to_act;
    if (actor != traverser) {
      const auto baseline =
          options_.use_opponent_value_baseline
              ? opponent_value_baselines_.frozen_values(key.value(), action_count)
              : Result<std::array<double, maximum_actions>, HuPreflopError>::success({});
      if (!baseline) {
        return Result<double, HuPreflopError>::failure(baseline.error());
      }
      std::array<double, maximum_actions> strategy_delta{};
      const auto multiplier = sampled_update_weight(options_.sampling_algorithm, iteration) *
                              external_sampling_average_multiplier(actor, traverser, reach[actor]);
      for (std::size_t action = 0U; action < action_count; ++action) {
        strategy_delta[action] = multiplier * strategy.value()[action];
      }
      const std::array<double, maximum_actions> regret_delta{};
      const auto update_index = pending.updates.size();
      if (!append_pending_update(pending, key.value(), action_count, iteration, regret_delta,
                                 strategy_delta)) {
        return Result<double, HuPreflopError>::failure(pending.error);
      }
      const auto selected =
          forced_opponent_response_quantile.has_value()
              ? external_sampling_action_from_quantile(strategy.value(), action_count,
                                                       *forced_opponent_response_quantile)
              : sample_action(strategy.value(), action_count, random);
      const auto next = apply_postflop_action(state, actions.value(), selected);
      if (!next) {
        return Result<double, HuPreflopError>::failure(next.error());
      }
      auto next_reach = reach;
      next_reach[actor] *= strategy.value()[selected];
      const auto child = collect_parallel_postflop(
          next.value(), deal, next_action_history(history, actions.value().actions[selected]),
          entry_node_id, traverser, iteration, next_reach, random, context, pending, std::nullopt);
      if (!child || !options_.use_opponent_value_baseline) {
        return child;
      }
      pending.updates[update_index].opponent_baseline_action = static_cast<std::uint8_t>(selected);
      pending.updates[update_index].opponent_baseline_observation = child.value();
      return Result<double, HuPreflopError>::success(external_sampling_opponent_baseline_estimate(
          strategy.value(), baseline.value(), action_count, selected, child.value()));
    }

    const auto stratify_after_this_decision =
        options_.root_first_opponent_response_stratification &&
        !pending.first_traverser_decision_seen;
    if (stratify_after_this_decision) {
      pending.first_traverser_decision_seen = true;
    }
    const auto child_opponent_response_quantile =
        forced_opponent_response_quantile.has_value() ? forced_opponent_response_quantile
        : stratify_after_this_decision
            ? std::optional<double>{pending.root_response_stratified_quantile}
            : std::nullopt;
    std::array<double, maximum_actions> action_values{};
    std::array<HuPreflopTelemetryTerminalType, maximum_actions> terminal_types{};
    double node_value = 0.0;
    const bool use_global_common_random_numbers = options_.global_common_random_numbers;
    const auto common_random_state = random;
    std::mt19937_64 first_action_random_state;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto next = apply_postflop_action(state, actions.value(), action);
      if (!next) {
        return Result<double, HuPreflopError>::failure(next.error());
      }
      auto next_reach = reach;
      next_reach[actor] *= strategy.value()[action];
      std::mt19937_64 action_random =
          use_global_common_random_numbers ? common_random_state : random;
      const auto value = collect_parallel_postflop(
          next.value(), deal, next_action_history(history, actions.value().actions[action]),
          entry_node_id, traverser, iteration, next_reach, action_random, context, pending,
          child_opponent_response_quantile);
      if (!value) {
        return value;
      }
      if (use_global_common_random_numbers && action == 0U) {
        first_action_random_state = action_random;
      } else if (!use_global_common_random_numbers) {
        random = action_random;
      }
      action_values[action] = value.value();
      terminal_types[action] = postflop_telemetry_terminal(state, next.value());
      node_value += strategy.value()[action] * action_values[action];
    }
    if (use_global_common_random_numbers && action_count != 0U) {
      random = first_action_random_state;
    }
    if (options_.collect_action_conditioned_telemetry) {
      for (std::size_t action = 0U; action < action_count; ++action) {
        observe_action_conditioned_telemetry(
            context.action_conditioned_telemetry, entry_node_id, key.value(), state,
            static_cast<std::uint8_t>(action), terminal_types[action], deal, reach,
            action_values[action], node_value, context.action_conditioned_telemetry_dropped,
            context.action_conditioned_telemetry_entry_limit);
      }
    }
    std::array<double, maximum_actions> regret_delta{};
    const auto weight = sampled_update_weight(options_.sampling_algorithm, iteration);
    for (std::size_t action = 0U; action < action_count; ++action) {
      regret_delta[action] = weight * (action_values[action] - node_value);
    }
    const std::array<double, maximum_actions> strategy_delta{};
    if (!append_pending_update(pending, key.value(), action_count, iteration, regret_delta,
                               strategy_delta)) {
      return Result<double, HuPreflopError>::failure(pending.error);
    }
    if (action_count > 1U) {
      const auto [minimum, maximum] =
          std::minmax_element(action_values.begin(), action_values.begin() + action_count);
      const auto spread = *maximum - *minimum;
      context.action_value_spread_total += spread;
      context.action_value_spread_maximum = std::max(context.action_value_spread_maximum, spread);
      ++context.action_value_spread_samples;
    }
    return Result<double, HuPreflopError>::success(node_value);
  }

  Result<double, HuPreflopError> collect_parallel_preflop(
      const std::uint32_t node_id, Deal &deal, const std::uint8_t traverser,
      const std::uint64_t iteration, const std::array<double, 2> &reach, std::mt19937_64 &random,
      ParallelWorkerContext &context, PendingTraversal &pending,
      const std::optional<double> forced_opponent_response_quantile = std::nullopt) const {
    if (node_id >= tree_.nodes.size()) {
      return Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    const auto &node = tree_.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::TerminalFold ||
        node.kind == HuPreflopNodeKind::TerminalAllIn) {
      if (node.kind == HuPreflopNodeKind::TerminalAllIn &&
          options_.preflop_all_in_training_oracle) {
        return exact_preflop_all_in_payoff(node.state, deal, traverser);
      }
      return parallel_terminal_payoff(node.state, deal, traverser, context);
    }
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      const auto advanced = advance_with_board(node.state, deal);
      if (!advanced) {
        return Result<double, HuPreflopError>::failure(advanced.error());
      }
      return collect_parallel_postflop(
          advanced.value(), deal, mix_history(postflop_history_seed, node.id), node.id, traverser,
          iteration, reach, random, context, pending, forced_opponent_response_quantile);
    }
    const auto actor = node.state.player_to_act;
    const auto key = preflop_key(node, deal);
    const auto action_count = node.edges.size();
    const auto strategy = blueprint_.frozen_current_strategy(key, action_count);
    if (!strategy) {
      return Result<double, HuPreflopError>::failure(strategy.error());
    }
    if (actor != traverser) {
      const auto baseline =
          options_.use_opponent_value_baseline
              ? opponent_value_baselines_.frozen_values(key, action_count)
              : Result<std::array<double, maximum_actions>, HuPreflopError>::success({});
      if (!baseline) {
        return Result<double, HuPreflopError>::failure(baseline.error());
      }
      std::array<double, maximum_actions> strategy_delta{};
      const auto multiplier = sampled_update_weight(options_.sampling_algorithm, iteration) *
                              external_sampling_average_multiplier(actor, traverser, reach[actor]);
      for (std::size_t action = 0U; action < action_count; ++action) {
        strategy_delta[action] = multiplier * strategy.value()[action];
      }
      const std::array<double, maximum_actions> regret_delta{};
      const auto update_index = pending.updates.size();
      if (!append_pending_update(pending, key, action_count, iteration, regret_delta,
                                 strategy_delta)) {
        return Result<double, HuPreflopError>::failure(pending.error);
      }
      const auto selected =
          forced_opponent_response_quantile.has_value()
              ? external_sampling_action_from_quantile(strategy.value(), action_count,
                                                       *forced_opponent_response_quantile)
              : sample_action(strategy.value(), action_count, random);
      auto next_reach = reach;
      next_reach[actor] *= strategy.value()[selected];
      const auto child =
          collect_parallel_preflop(node.edges[selected].child, deal, traverser, iteration,
                                   next_reach, random, context, pending, std::nullopt);
      if (!child || !options_.use_opponent_value_baseline) {
        return child;
      }
      pending.updates[update_index].opponent_baseline_action = static_cast<std::uint8_t>(selected);
      pending.updates[update_index].opponent_baseline_observation = child.value();
      return Result<double, HuPreflopError>::success(external_sampling_opponent_baseline_estimate(
          strategy.value(), baseline.value(), action_count, selected, child.value()));
    }

    const auto stratify_after_this_decision =
        options_.root_first_opponent_response_stratification &&
        !pending.first_traverser_decision_seen;
    if (stratify_after_this_decision) {
      pending.first_traverser_decision_seen = true;
    }
    const auto child_opponent_response_quantile =
        forced_opponent_response_quantile.has_value() ? forced_opponent_response_quantile
        : stratify_after_this_decision
            ? std::optional<double>{pending.root_response_stratified_quantile}
            : std::nullopt;
    const auto continuation_update_begin = pending.updates.size();
    std::array<double, maximum_actions> action_values{};
    double node_value = 0.0;
    const auto common_root_action_seed =
        mix_history(pending.random_seed ^ 0x4352'4E52'4F4F'5400ULL, iteration);
    const bool use_root_common_random_numbers =
        node_id == tree_.root && options_.root_common_random_numbers;
    const bool use_global_common_random_numbers = options_.global_common_random_numbers;
    const auto common_random_state = random;
    std::mt19937_64 first_action_random_state;
    for (std::size_t action = 0U; action < action_count; ++action) {
      auto next_reach = reach;
      next_reach[actor] *= strategy.value()[action];
      std::mt19937_64 action_random =
          use_global_common_random_numbers ? common_random_state : random;
      if (use_root_common_random_numbers) {
        action_random.seed(common_root_action_seed);
      }
      const auto value = collect_parallel_preflop(node.edges[action].child, deal, traverser,
                                                  iteration, next_reach, action_random, context,
                                                  pending, child_opponent_response_quantile);
      if (!value) {
        return value;
      }
      if (use_global_common_random_numbers && !use_root_common_random_numbers && action == 0U) {
        first_action_random_state = action_random;
      } else if (!use_global_common_random_numbers && !use_root_common_random_numbers) {
        random = action_random;
      }
      action_values[action] = value.value();
      node_value += strategy.value()[action] * action_values[action];
    }
    if (use_global_common_random_numbers && !use_root_common_random_numbers && action_count != 0U) {
      random = first_action_random_state;
    }
    if (node_id == tree_.root && options_.root_action_value_rollouts > 1U) {
      auto action_value_totals = action_values;
      PendingTraversal shadow;
      const auto continuation_scale =
          1.0 / static_cast<double>(options_.root_action_value_rollouts);
      if (options_.root_continuation_mean_updates) {
        scale_pending_updates(pending, continuation_update_begin, continuation_scale);
      }
      for (std::uint8_t rollout = 1U; rollout < options_.root_action_value_rollouts; ++rollout) {
        auto deal_seed = mix_history(pending.random_seed ^ 0x524F'4F54'4445'414CULL,
                                     (iteration << 8U) | rollout);
        std::mt19937_64 deal_random(deal_seed);
        const auto conditioned =
            sample_deal_conditioned_on_class(key.preflop_class, traverser, deal_random);
        if (!conditioned) {
          return Result<double, HuPreflopError>::failure(conditioned.error());
        }
        for (std::size_t action = 0U; action < action_count; ++action) {
          shadow.updates.clear();
          shadow.error = HuPreflopError::InvalidConfiguration;
          shadow.first_traverser_decision_seen = true;
          shadow.root_response_stratified_quantile =
              options_.root_first_opponent_response_stratification
                  ? root_response_stratified_quantile(pending.random_seed, iteration, rollout,
                                                      options_.root_action_value_rollouts)
                  : 0.0;
          auto action_deal = conditioned.value();
          const auto action_seed =
              options_.root_common_random_numbers
                  ? mix_history(deal_seed ^ 0x4352'4E53'4844'5700ULL, rollout)
                  : mix_history(deal_seed ^ 0x524F'4F54'4143'544EULL, action + 1U);
          std::mt19937_64 action_random(action_seed);
          const auto value = collect_parallel_preflop(
              node.edges[action].child, action_deal, traverser, iteration, reach, action_random,
              context, shadow,
              options_.root_first_opponent_response_stratification
                  ? std::optional<double>{shadow.root_response_stratified_quantile}
                  : std::nullopt);
          context.peak_shadow_updates =
              std::max<std::uint64_t>(context.peak_shadow_updates, shadow.updates.capacity());
          if (!value) {
            return value;
          }
          action_value_totals[action] += value.value();
          if (options_.root_continuation_mean_updates &&
              !append_scaled_pending_updates(pending, shadow, continuation_scale)) {
            return Result<double, HuPreflopError>::failure(pending.error);
          }
        }
      }
      node_value = 0.0;
      for (std::size_t action = 0U; action < action_count; ++action) {
        action_values[action] =
            action_value_totals[action] / static_cast<double>(options_.root_action_value_rollouts);
        node_value += strategy.value()[action] * action_values[action];
      }
    }
    std::array<double, maximum_actions> regret_delta{};
    const auto weight = sampled_update_weight(options_.sampling_algorithm, iteration);
    for (std::size_t action = 0U; action < action_count; ++action) {
      regret_delta[action] = weight * (action_values[action] - node_value);
    }
    const std::array<double, maximum_actions> strategy_delta{};
    if (!append_pending_update(pending, key, action_count, iteration, regret_delta,
                               strategy_delta)) {
      return Result<double, HuPreflopError>::failure(pending.error);
    }
    if (node_id == tree_.root) {
      auto &update = pending.updates.back();
      update.has_root_action_advantage = true;
      for (std::size_t action = 0U; action < action_count; ++action) {
        update.root_action_advantage[action] = action_values[action] - node_value;
      }
    }
    return Result<double, HuPreflopError>::success(node_value);
  }

  Result<bool, HuPreflopError> collect_parallel_btn_mean_rollouts(const Deal &primary_deal,
                                                                  ParallelWorkerContext &context,
                                                                  PendingTraversal &pending) const {
    if (pending.traverser != 1U) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    const auto continuation_scale = 1.0 / static_cast<double>(options_.root_action_value_rollouts);
    scale_pending_updates(pending, 0U, continuation_scale);
    const auto class_id = deal_class(primary_deal, pending.traverser);
    PendingTraversal shadow;
    for (std::uint8_t rollout = 1U; rollout < options_.root_action_value_rollouts; ++rollout) {
      const auto deal_seed = mix_history(pending.random_seed ^ 0x4254'4E4D'4541'4E44ULL,
                                         (pending.iteration << 8U) | rollout);
      std::mt19937_64 deal_random(deal_seed);
      const auto conditioned =
          sample_deal_conditioned_on_class(class_id, pending.traverser, deal_random);
      if (!conditioned) {
        return Result<bool, HuPreflopError>::failure(conditioned.error());
      }
      shadow.updates.clear();
      shadow.error = HuPreflopError::InvalidConfiguration;
      shadow.random_seed = deal_seed;
      shadow.iteration = pending.iteration;
      shadow.traverser = pending.traverser;
      shadow.first_traverser_decision_seen = false;
      shadow.root_response_stratified_quantile =
          options_.root_first_opponent_response_stratification
              ? root_response_stratified_quantile(pending.random_seed, pending.iteration, rollout,
                                                  options_.root_action_value_rollouts)
              : 0.0;
      auto deal = conditioned.value();
      std::mt19937_64 traversal_random(mix_history(deal_seed ^ 0x4254'4E54'5241'5652ULL, rollout));
      const std::array<double, 2> reach{1.0, 1.0};
      const auto value =
          collect_parallel_preflop(tree_.root, deal, pending.traverser, pending.iteration, reach,
                                   traversal_random, context, shadow);
      context.peak_shadow_updates =
          std::max<std::uint64_t>(context.peak_shadow_updates, shadow.updates.capacity());
      if (!value) {
        return Result<bool, HuPreflopError>::failure(value.error());
      }
      if (!append_scaled_pending_updates(pending, shadow, continuation_scale)) {
        return Result<bool, HuPreflopError>::failure(pending.error);
      }
    }
    return Result<bool, HuPreflopError>::success(true);
  }

  Result<bool, HuPreflopError> train_parallel_batches() {
    try {
      std::vector<ParallelWorkerContext> contexts(options_.worker_threads);
      const auto cache_capacity = options_.maximum_bucket_cache_entries /
                                  (static_cast<std::uint64_t>(options_.worker_threads) * 6U);
      for (auto &context : contexts) {
        context.action_conditioned_telemetry_entry_limit =
            (options_.maximum_action_conditioned_telemetry_entries +
             static_cast<std::uint64_t>(options_.worker_threads) - 1U) /
            static_cast<std::uint64_t>(options_.worker_threads);
        for (auto &player_caches : context.bucket_cache) {
          for (auto &cache : player_caches) {
            cache.set_capacity(cache_capacity);
          }
        }
        context.exact_postflop_all_in_cache.set_capacity(
            options_.maximum_exact_postflop_all_in_cache_entries /
            static_cast<std::uint64_t>(options_.worker_threads));
      }

      for (std::uint64_t batch_begin = 1U; batch_begin <= options_.iterations;) {
        const auto batch_end = std::min<std::uint64_t>(
            options_.iterations, batch_begin + options_.training_batch_iterations - 1U);
        const auto batch_iterations = batch_end - batch_begin + 1U;
        std::vector<PendingTraversal> jobs(static_cast<std::size_t>(batch_iterations * 2U));
        for (std::size_t job_index = 0U; job_index < jobs.size(); ++job_index) {
          jobs[job_index].iteration = batch_begin + job_index / 2U;
          jobs[job_index].traverser = static_cast<std::uint8_t>(job_index % 2U);
          jobs[job_index].random_seed = random_();
          jobs[job_index].root_response_stratified_quantile =
              options_.root_first_opponent_response_stratification
                  ? root_response_stratified_quantile(jobs[job_index].random_seed,
                                                      jobs[job_index].iteration, 0U,
                                                      options_.root_action_value_rollouts)
                  : 0.0;
        }
        if (options_.chance_sampling_mode == HuPreflopChanceSamplingMode::PublicBoardStratified) {
          auto shuffled_deck = short_deck();
          std::shuffle(shuffled_deck.begin(), shuffled_deck.end(), random_);
          std::array<CardId, 5> board{};
          std::copy_n(shuffled_deck.begin(), board.size(), board.begin());
          for (std::uint8_t traverser = 0U; traverser < 2U; ++traverser) {
            auto private_deals = sample_public_board_private_deals(
                board, traverser, static_cast<std::uint32_t>(batch_iterations), random_);
            if (!private_deals) {
              return Result<bool, HuPreflopError>::failure(private_deals.error());
            }
            for (std::size_t sample = 0U; sample < private_deals.value().size(); ++sample) {
              auto &job = jobs[sample * 2U + traverser];
              job.fixed_deal.holes = private_deals.value()[sample].holes;
              job.fixed_deal.board = board;
              job.has_fixed_deal = true;
            }
          }
        }

        std::array<std::thread, 8> threads;
        std::size_t started_threads = 0U;
        try {
          for (; started_threads < options_.worker_threads; ++started_threads) {
            threads[started_threads] = std::thread([&, worker = started_threads] {
              try {
                for (std::size_t job_index = worker; job_index < jobs.size();
                     job_index += options_.worker_threads) {
                  auto &job = jobs[job_index];
                  std::mt19937_64 job_random(job.random_seed);
                  auto deal = job.has_fixed_deal ? job.fixed_deal : sample_deal(job_random);
                  const std::array<double, 2> reach{1.0, 1.0};
                  const auto value =
                      collect_parallel_preflop(tree_.root, deal, job.traverser, job.iteration,
                                               reach, job_random, contexts[worker], job);
                  if (!value) {
                    if (job.error == HuPreflopError::InvalidConfiguration) {
                      job.error = value.error();
                    }
                    continue;
                  }
                  if (options_.symmetric_traverser_mean_updates && job.traverser == 1U) {
                    const auto averaged =
                        collect_parallel_btn_mean_rollouts(deal, contexts[worker], job);
                    if (!averaged && job.error == HuPreflopError::InvalidConfiguration) {
                      job.error = averaged.error();
                    }
                  }
                }
              } catch (const std::bad_alloc &) {
                for (std::size_t job_index = worker; job_index < jobs.size();
                     job_index += options_.worker_threads) {
                  jobs[job_index].error = HuPreflopError::MemoryFailure;
                }
              } catch (...) {
                for (std::size_t job_index = worker; job_index < jobs.size();
                     job_index += options_.worker_threads) {
                  jobs[job_index].error = HuPreflopError::NumericalFailure;
                }
              }
            });
          }
        } catch (...) {
          for (std::size_t thread = 0U; thread < started_threads; ++thread) {
            if (threads[thread].joinable()) {
              threads[thread].join();
            }
          }
          return Result<bool, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
        }
        for (std::size_t thread = 0U; thread < started_threads; ++thread) {
          threads[thread].join();
        }

        for (const auto &job : jobs) {
          if (job.error != HuPreflopError::InvalidConfiguration) {
            return Result<bool, HuPreflopError>::failure(job.error);
          }
          peak_parallel_updates_per_job_ =
              std::max<std::uint64_t>(peak_parallel_updates_per_job_, job.updates.size());
          for (const auto &update : job.updates) {
            auto &information = blueprint_.touch(update.key, update.action_count, update.iteration);
            if (!blueprint_.valid()) {
              return Result<bool, HuPreflopError>::failure(blueprint_.error());
            }
            for (std::size_t action = 0U; action < update.action_count; ++action) {
              information.regrets[action] += update.regret_delta[action];
              information.strategy_sum[action] += update.strategy_sum_delta[action];
            }
            if (update.has_root_action_advantage &&
                !observe_root_action_advantage(update.key.preflop_class,
                                               update.root_action_advantage, update.action_count,
                                               update.iteration)) {
              return Result<bool, HuPreflopError>::failure(failure_);
            }
            if (update.opponent_baseline_action != std::numeric_limits<std::uint8_t>::max() &&
                !opponent_value_baselines_.observe(update.key, update.action_count,
                                                   update.opponent_baseline_action,
                                                   update.opponent_baseline_observation)) {
              return Result<bool, HuPreflopError>::failure(opponent_value_baselines_.error());
            }
          }
        }
        std::uint64_t scratch_payload = jobs.size() * sizeof(PendingTraversal);
        for (const auto &job : jobs) {
          scratch_payload += job.updates.capacity() * sizeof(PendingInformationUpdate);
        }
        for (const auto &context : contexts) {
          scratch_payload += context.peak_shadow_updates * sizeof(PendingInformationUpdate);
        }
        if (scratch_payload > options_.maximum_parallel_scratch_bytes) {
          return Result<bool, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
        }
        peak_parallel_scratch_payload_bytes_ =
            std::max(peak_parallel_scratch_payload_bytes_, scratch_payload);
        batch_begin = batch_end + 1U;
      }

      std::uint64_t parallel_cache_peak = 0U;
      for (const auto &context : contexts) {
        std::uint64_t worker_cache_peak = 0U;
        for (const auto &player_caches : context.bucket_cache) {
          for (const auto &cache : player_caches) {
            worker_cache_peak += cache.peak_entries();
            bucket_cache_evictions_from_parallel_ += cache.evictions();
          }
        }
        parallel_cache_peak += worker_cache_peak;
        for (std::size_t street = 0U; street < 3U; ++street) {
          bucket_mapping_visits_[street] += context.bucket_mapping_visits[street];
          bucket_mapping_computations_[street] += context.bucket_mapping_computations[street];
          occupied_distributional_buckets_[street] |= context.occupied_buckets[street];
        }
        bucket_mapping_seconds_ += context.bucket_mapping_seconds;
        winner_cache_hits_ += context.winner_cache_hits;
        winner_cache_misses_ += context.winner_cache_misses;
        for (std::size_t street = 0U; street < 2U; ++street) {
          exact_postflop_all_in_evaluations_[street] +=
              context.exact_postflop_all_in_evaluations[street];
          exact_postflop_all_in_runouts_[street] += context.exact_postflop_all_in_runouts[street];
        }
        exact_postflop_all_in_seconds_ += context.exact_postflop_all_in_seconds;
        exact_postflop_all_in_cache_hits_ += context.exact_postflop_all_in_cache_hits;
        exact_postflop_all_in_cache_misses_ += context.exact_postflop_all_in_cache_misses;
        parallel_exact_postflop_all_in_cache_peak_entries_ +=
            context.exact_postflop_all_in_cache.peak_entries();
        parallel_exact_postflop_all_in_cache_evictions_ +=
            context.exact_postflop_all_in_cache.evictions();
        postflop_action_value_spread_samples_ += context.action_value_spread_samples;
        postflop_action_value_spread_total_ += context.action_value_spread_total;
        postflop_action_value_spread_maximum_ =
            std::max(postflop_action_value_spread_maximum_, context.action_value_spread_maximum);
        peak_parallel_shadow_updates_per_worker_ =
            std::max(peak_parallel_shadow_updates_per_worker_, context.peak_shadow_updates);
        if (options_.collect_action_conditioned_telemetry) {
          action_conditioned_telemetry_dropped_ += context.action_conditioned_telemetry_dropped;
          for (const auto &[key, accumulator] : context.action_conditioned_telemetry) {
            auto entry = action_conditioned_telemetry_.find(key);
            if (entry == action_conditioned_telemetry_.end() &&
                action_conditioned_telemetry_.size() >=
                    options_.maximum_action_conditioned_telemetry_entries) {
              action_conditioned_telemetry_dropped_ += accumulator.sample_count;
              continue;
            }
            if (entry == action_conditioned_telemetry_.end()) {
              entry = action_conditioned_telemetry_
                          .emplace(key, ActionConditionedTelemetryAccumulator{})
                          .first;
            }
            entry->second.merge(accumulator);
          }
        }
      }
      parallel_bucket_cache_peak_entries_ = parallel_cache_peak;
      return Result<bool, HuPreflopError>::success(true);
    } catch (const std::bad_alloc &) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
    }
  }

  Result<PostflopActionView, HuPreflopError> postflop_actions(const PublicState &state,
                                                              const std::uint64_t history) const {
    PostflopActionView view;
    if (compiled_betting_.has_value()) {
      const auto entry = compiled_betting_->history_to_node.find(history);
      if (entry == compiled_betting_->history_to_node.end()) {
        return Result<PostflopActionView, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      const auto &node = compiled_betting_->nodes[entry->second];
      if (!same_betting_state(node.state_template, state)) {
        return Result<PostflopActionView, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      view.actions = node.actions;
      view.action_count = node.action_count;
      view.compiled = &node;
      return Result<PostflopActionView, HuPreflopError>::success(view);
    }
    const auto legal = legal_actions(state, postflop_config_);
    if (!legal || legal.value().empty() || legal.value().size() > maximum_actions) {
      return Result<PostflopActionView, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    view.action_count = static_cast<std::uint8_t>(legal.value().size());
    std::ranges::copy(legal.value(), view.actions.begin());
    return Result<PostflopActionView, HuPreflopError>::success(view);
  }

  Result<PublicState, HuPreflopError> apply_postflop_action(const PublicState &state,
                                                            const PostflopActionView &actions,
                                                            const std::size_t action) const {
    if (action >= actions.action_count) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    if (actions.compiled != nullptr) {
      auto next = actions.compiled->next_states[action];
      next.board_mask = state.board_mask;
      return validate_state(next)
                 ? Result<PublicState, HuPreflopError>::success(next)
                 : Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    const auto next = apply_action(state, actions.actions[action], postflop_config_);
    return next ? Result<PublicState, HuPreflopError>::success(next.value())
                : Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }

  InformationKey preflop_key(const HuPreflopNode &node, const Deal &deal) const {
    InformationKey key;
    key.public_history = node.id;
    key.preflop_class = deal_class(deal, node.state.player_to_act);
    key.player = node.state.player_to_act;
    return key;
  }

  Result<InformationKey, HuPreflopError> postflop_key(const PublicState &state, Deal &deal,
                                                      const std::uint64_t history) {
    InformationKey key;
    key.public_history = history;
    key.player = state.player_to_act;
    key.street = state.street;
    const bool forgets_preflop_class =
        representation_forgets_preflop_class(options_.postflop_representation);
    key.preflop_class =
        forgets_preflop_class ? HandClassId{0U} : deal_class(deal, state.player_to_act);
    if (options_.postflop_representation == HuPreflopPostflopRepresentation::ExactPhysical) {
      key.physical_cards = physical_cards_key(deal, state.player_to_act, state.street);
      return Result<InformationKey, HuPreflopError>::success(key);
    }
    const auto final_index =
        static_cast<std::size_t>(state.street) - static_cast<std::size_t>(Street::Flop);
    const auto current_street_only =
        representation_uses_current_street_only(options_.postflop_representation);
    const auto first_index =
        uses_one_street_history(options_.postflop_representation) && final_index > 0U
            ? final_index - 1U
        : current_street_only ? final_index
                              : 0U;
    for (std::size_t index = first_index; index <= final_index; ++index) {
      ++bucket_mapping_visits_[index];
      const auto bucket_street =
          static_cast<Street>(static_cast<std::size_t>(Street::Flop) + index);
      auto &bucket = deal.buckets[state.player_to_act][index];
      if (bucket == unset_bucket) {
        const auto observation_key = physical_cards_key(deal, state.player_to_act, bucket_street);
        auto &cache = bucket_cache_[state.player_to_act][index];
        const auto cached = cache.find(observation_key);
        if (cached.has_value()) {
          bucket = cached.value();
        } else {
          const auto mapping_started = std::chrono::steady_clock::now();
          const auto computed =
              is_distributional_strength_representation(options_.postflop_representation)
                  ? compute_distributional_strength_bucket(
                        deal, state.player_to_act, bucket_street,
                        options_.equity_samples_per_bucket, options_.partition_seed,
                        options_.distributional_bucket_capacities,
                        uses_distributional_profile_v6(options_.postflop_representation),
                        uses_distributional_structured_v7(options_.postflop_representation),
                        uses_distributional_street_adaptive_v8(options_.postflop_representation))
                  : compute_bucket(deal, state.player_to_act, bucket_street,
                                   options_.equity_samples_per_bucket, options_.partition_seed);
          if (!computed) {
            return Result<InformationKey, HuPreflopError>::failure(computed.error());
          }
          bucket_mapping_seconds_ +=
              std::chrono::duration<double>(std::chrono::steady_clock::now() - mapping_started)
                  .count();
          ++bucket_mapping_computations_[index];
          bucket = computed.value();
          cache.insert(observation_key, bucket);
        }
      }
      if (is_distributional_strength_representation(options_.postflop_representation)) {
        occupied_distributional_buckets_[index].set(bucket);
      }
      const auto information_bucket = distributional_information_bucket(
          deal, state.player_to_act, bucket_street, index, final_index, bucket,
          options_.distributional_bucket_capacities[index], options_.postflop_representation);
      if (!information_bucket) {
        return Result<InformationKey, HuPreflopError>::failure(information_bucket.error());
      }
      key.bucket_history[index] = information_bucket.value();
    }
    return Result<InformationKey, HuPreflopError>::success(key);
  }

  Result<PublicState, HuPreflopError> advance_with_board(const PublicState &state,
                                                         const Deal &deal) const {
    const auto advanced = advance_street(state);
    if (!advanced) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    auto next = advanced.value();
    if (next.street == Street::Flop) {
      next.board_mask |= deal.board[0].mask() | deal.board[1].mask() | deal.board[2].mask();
    } else if (next.street == Street::Turn) {
      next.board_mask |= deal.board[3].mask();
    } else {
      next.board_mask |= deal.board[4].mask();
    }
    if (!validate_state(next)) {
      return Result<PublicState, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    return Result<PublicState, HuPreflopError>::success(next);
  }

  double terminal_payoff(const PublicState &state, Deal &deal, const std::uint8_t player) {
    if (should_integrate_postflop_all_in(state)) {
      const auto started = std::chrono::steady_clock::now();
      const auto expected = exact_postflop_all_in_payoff(
          state, deal, player, exact_postflop_all_in_cache_, exact_postflop_all_in_cache_hits_,
          exact_postflop_all_in_cache_misses_);
      exact_postflop_all_in_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      if (!expected) {
        failure_ = expected.error();
        return 0.0;
      }
      const auto street_index = state.street == Street::Flop ? 0U : 1U;
      ++exact_postflop_all_in_evaluations_[street_index];
      exact_postflop_all_in_runouts_[street_index] += expected.value().second;
      return expected.value().first;
    }
    std::uint8_t winner_mask = 0U;
    if (state.status != HandStatus::Folded) {
      if (deal.winner_mask < 0) {
        const std::vector<std::array<CardId, 2>> holes{deal.holes[0], deal.holes[1]};
        const std::vector<CardId> board(deal.board.begin(), deal.board.end());
        const auto showdown = seven_card_table_.has_value()
                                  ? evaluate_showdown(holes, board, *seven_card_table_)
                                  : evaluate_showdown(holes, board);
        if (!showdown) {
          failure_ = HuPreflopError::EquityFailure;
          return 0.0;
        }
        deal.winner_mask = static_cast<std::int8_t>(showdown.value().winner_mask);
        ++winner_cache_misses_;
      } else {
        ++winner_cache_hits_;
      }
      winner_mask = static_cast<std::uint8_t>(deal.winner_mask);
    }
    const auto settlement = settle_terminal(state, tree_.config.rake, winner_mask);
    if (!settlement) {
      failure_ = HuPreflopError::GameFailure;
      return 0.0;
    }
    return static_cast<double>(settlement.value().payoff_units[player]) /
           static_cast<double>(Money::units_per_ante);
  }

  double preflop_terminal_payoff(const HuPreflopNode &node, Deal &deal, const std::uint8_t player) {
    if (node.kind == HuPreflopNodeKind::TerminalAllIn && options_.preflop_all_in_training_oracle) {
      const auto expected = exact_preflop_all_in_payoff(node.state, deal, player);
      if (!expected) {
        failure_ = expected.error();
        return 0.0;
      }
      return expected.value();
    }
    return terminal_payoff(node.state, deal, player);
  }

  double traverse_full_postflop(const PublicState &state, Deal &deal, const std::uint64_t history,
                                const std::uint8_t traverser, const std::uint64_t iteration,
                                const std::array<double, 2> &reach) {
    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
        state.status == HandStatus::Showdown) {
      return terminal_payoff(state, deal, traverser);
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_with_board(state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return traverse_full_postflop(
          advanced.value(), deal,
          mix_history(history, 0x4348'414E'4345'0000ULL +
                                   static_cast<std::uint64_t>(advanced.value().street)),
          traverser, iteration, reach);
    }
    const auto actions = postflop_actions(state, history);
    const auto key = postflop_key(state, deal, history);
    if (!actions || !key) {
      failure_ = !actions ? actions.error() : key.error();
      return 0.0;
    }
    const auto action_count = actions.value().action_count;
    auto &information = blueprint_.touch(key.value(), action_count, iteration);
    const auto strategy = blueprint_.current_strategy(information);
    const auto actor = state.player_to_act;
    if (actor != traverser) {
      for (std::size_t action = 0U; action < action_count; ++action) {
        information.strategy_sum[action] +=
            chance_sampled_cfr_average_multiplier(reach[actor]) * strategy[action];
      }
    }

    std::array<double, maximum_actions> action_values{};
    double node_value = 0.0;
    for (std::size_t action = 0U; action < action_count; ++action) {
      const auto next = apply_postflop_action(state, actions.value(), action);
      if (!next) {
        failure_ = next.error();
        return 0.0;
      }
      auto next_reach = reach;
      next_reach[actor] *= strategy[action];
      action_values[action] = traverse_full_postflop(
          next.value(), deal, next_action_history(history, actions.value().actions[action]),
          traverser, iteration, next_reach);
      node_value += strategy[action] * action_values[action];
    }
    if (actor == traverser) {
      const auto counterfactual_reach = reach[1U - actor];
      for (std::size_t action = 0U; action < action_count; ++action) {
        information.regrets[action] += chance_sampled_cfr_regret_delta(
            action_values[action], node_value, counterfactual_reach);
      }
    }
    return node_value;
  }

  double traverse_full_preflop(const std::uint32_t node_id, Deal &deal,
                               const std::uint8_t traverser, const std::uint64_t iteration,
                               const std::array<double, 2> &reach) {
    if (node_id >= tree_.nodes.size()) {
      failure_ = HuPreflopError::GameFailure;
      return 0.0;
    }
    const auto &node = tree_.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::TerminalFold ||
        node.kind == HuPreflopNodeKind::TerminalAllIn) {
      return preflop_terminal_payoff(node, deal, traverser);
    }
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      const auto advanced = advance_with_board(node.state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return traverse_full_postflop(advanced.value(), deal,
                                    mix_history(postflop_history_seed, node.id), traverser,
                                    iteration, reach);
    }
    const auto actor = node.state.player_to_act;
    const auto key = preflop_key(node, deal);
    const auto action_count = node.edges.size();
    auto &information = blueprint_.touch(key, action_count, iteration);
    const auto strategy = blueprint_.current_strategy(information);
    if (actor != traverser) {
      for (std::size_t action = 0U; action < action_count; ++action) {
        information.strategy_sum[action] +=
            chance_sampled_cfr_average_multiplier(reach[actor]) * strategy[action];
      }
    }

    std::array<double, maximum_actions> action_values{};
    double node_value = 0.0;
    for (std::size_t action = 0U; action < action_count; ++action) {
      auto next_reach = reach;
      next_reach[actor] *= strategy[action];
      action_values[action] =
          traverse_full_preflop(node.edges[action].child, deal, traverser, iteration, next_reach);
      node_value += strategy[action] * action_values[action];
    }
    if (actor == traverser) {
      const auto counterfactual_reach = reach[1U - actor];
      for (std::size_t action = 0U; action < action_count; ++action) {
        information.regrets[action] += chance_sampled_cfr_regret_delta(
            action_values[action], node_value, counterfactual_reach);
      }
    }
    return node_value;
  }

  std::array<double, maximum_actions> policy_strategy(const DcfrTable &policy,
                                                      const InformationKey &key,
                                                      const std::size_t action_count,
                                                      const SolverPolicyView view) {
    if (view == SolverPolicyView::Average) {
      return policy.average_strategy(key, action_count);
    }
    const auto current = policy.frozen_current_strategy(key, action_count);
    if (!current) {
      failure_ = current.error();
      return {};
    }
    return current.value();
  }

  double traverse_preflop(const std::uint32_t node_id, Deal &deal, DcfrTable &training,
                          const DcfrTable *fixed, const std::uint8_t traverser,
                          const std::uint64_t iteration, const std::array<double, 2> &reach,
                          const bool response_mode,
                          const SolverPolicyView fixed_policy_view = SolverPolicyView::Average) {
    if (node_id >= tree_.nodes.size()) {
      failure_ = HuPreflopError::GameFailure;
      return 0.0;
    }
    const auto &node = tree_.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::TerminalFold ||
        node.kind == HuPreflopNodeKind::TerminalAllIn) {
      return preflop_terminal_payoff(node, deal, traverser);
    }
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      const auto advanced = advance_with_board(node.state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      const auto history = mix_history(postflop_history_seed, node.id);
      return traverse_postflop(advanced.value(), deal, history, node.id, training, fixed, traverser,
                               iteration, reach, response_mode, fixed_policy_view);
    }

    const auto actor = node.state.player_to_act;
    const auto key = preflop_key(node, deal);
    const auto action_count = node.edges.size();
    const bool fixed_actor = response_mode && actor != traverser;
    std::array<double, maximum_actions> strategy{};
    InformationState *information = nullptr;
    if (fixed_actor) {
      strategy = policy_strategy(*fixed, key, action_count, fixed_policy_view);
      if (failure_ != HuPreflopError::InvalidConfiguration) {
        return 0.0;
      }
    } else {
      information = &training.touch(key, action_count, iteration);
      strategy = training.current_strategy(*information);
      const auto average_pass = response_mode ? ExternalSamplingAveragePass::OneSidedTraverser
                                              : ExternalSamplingAveragePass::OpponentOfTraverser;
      const auto average_multiplier =
          sampled_update_weight(options_.sampling_algorithm, iteration) *
          external_sampling_average_multiplier(actor, traverser, reach[actor], average_pass);
      for (std::size_t action = 0; action < action_count; ++action) {
        information->strategy_sum[action] += average_multiplier * strategy[action];
      }
    }
    if (actor != traverser) {
      const auto selected = sample_action(strategy, action_count, random_);
      auto next_reach = reach;
      next_reach[actor] *= strategy[selected];
      return traverse_preflop(node.edges[selected].child, deal, training, fixed, traverser,
                              iteration, next_reach, response_mode, fixed_policy_view);
    }

    std::array<double, maximum_actions> action_values{};
    std::array<HuPreflopTelemetryTerminalType, maximum_actions> terminal_types{};
    double node_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
      auto next_reach = reach;
      next_reach[actor] *= strategy[action];
      action_values[action] =
          traverse_preflop(node.edges[action].child, deal, training, fixed, traverser, iteration,
                           next_reach, response_mode, fixed_policy_view);
      terminal_types[action] = preflop_telemetry_terminal(tree_.nodes[node.edges[action].child]);
      node_value += strategy[action] * action_values[action];
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      information->regrets[action] +=
          sampled_update_weight(options_.sampling_algorithm, iteration) *
          (action_values[action] - node_value);
    }
    if (!response_mode && node_id == tree_.root && &training == &blueprint_ &&
        !observe_root_action_advantage(key.preflop_class, action_values, action_count, iteration,
                                       node_value)) {
      return 0.0;
    }
    if (!response_mode && &training == &blueprint_ &&
        options_.collect_action_conditioned_telemetry) {
      for (std::size_t action = 0U; action < action_count; ++action) {
        observe_action_conditioned_telemetry(
            action_conditioned_telemetry_, node.id, key, node.state,
            static_cast<std::uint8_t>(action), terminal_types[action], deal, reach,
            action_values[action], node_value, action_conditioned_telemetry_dropped_,
            options_.maximum_action_conditioned_telemetry_entries);
      }
    }
    return node_value;
  }

  double traverse_preflop_refinement(const std::uint32_t node_id, Deal &deal,
                                     const std::uint8_t traverser, const std::uint64_t iteration,
                                     const std::array<double, 2> &reach) {
    if (node_id >= tree_.nodes.size()) {
      failure_ = HuPreflopError::GameFailure;
      return 0.0;
    }
    const auto &node = tree_.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::TerminalFold ||
        node.kind == HuPreflopNodeKind::TerminalAllIn) {
      return preflop_terminal_payoff(node, deal, traverser);
    }
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      const auto advanced = advance_with_board(node.state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return play_postflop(advanced.value(), deal, mix_history(postflop_history_seed, node.id),
                           blueprint_, blueprint_, random_, traverser);
    }

    const auto actor = node.state.player_to_act;
    const auto key = preflop_key(node, deal);
    const auto action_count = node.edges.size();
    auto &information = blueprint_.touch(key, action_count, iteration);
    const auto strategy = blueprint_.current_strategy(information);
    const auto average_multiplier =
        sampled_update_weight(options_.sampling_algorithm, iteration) *
        external_sampling_average_multiplier(actor, traverser, reach[actor]);
    for (std::size_t action = 0U; action < action_count; ++action) {
      information.strategy_sum[action] += average_multiplier * strategy[action];
    }
    if (actor != traverser) {
      const auto selected = sample_action(strategy, action_count, random_);
      auto next_reach = reach;
      next_reach[actor] *= strategy[selected];
      return traverse_preflop_refinement(node.edges[selected].child, deal, traverser, iteration,
                                         next_reach);
    }

    std::array<double, maximum_actions> action_values{};
    double node_value = 0.0;
    for (std::size_t action = 0U; action < action_count; ++action) {
      auto next_reach = reach;
      next_reach[actor] *= strategy[action];
      action_values[action] = traverse_preflop_refinement(node.edges[action].child, deal, traverser,
                                                          iteration, next_reach);
      node_value += strategy[action] * action_values[action];
    }
    for (std::size_t action = 0U; action < action_count; ++action) {
      information.regrets[action] += sampled_update_weight(options_.sampling_algorithm, iteration) *
                                     (action_values[action] - node_value);
    }
    return node_value;
  }

  double traverse_postflop(const PublicState &state, Deal &deal, const std::uint64_t history,
                           const std::uint32_t entry_node_id, DcfrTable &training,
                           const DcfrTable *fixed, const std::uint8_t traverser,
                           const std::uint64_t iteration, const std::array<double, 2> &reach,
                           const bool response_mode,
                           const SolverPolicyView fixed_policy_view = SolverPolicyView::Average) {
    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
        state.status == HandStatus::Showdown) {
      return terminal_payoff(state, deal, traverser);
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_with_board(state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return traverse_postflop(
          advanced.value(), deal,
          mix_history(history, 0x4348'414E'4345'0000ULL +
                                   static_cast<std::uint64_t>(advanced.value().street)),
          entry_node_id, training, fixed, traverser, iteration, reach, response_mode,
          fixed_policy_view);
    }
    const auto actions = postflop_actions(state, history);
    const auto key_result = postflop_key(state, deal, history);
    if (!actions || !key_result) {
      failure_ = !actions ? actions.error() : key_result.error();
      return 0.0;
    }
    const auto &key = key_result.value();
    const auto action_count = actions.value().action_count;
    const auto actor = state.player_to_act;
    const bool fixed_actor = response_mode && actor != traverser;
    std::array<double, maximum_actions> strategy{};
    InformationState *information = nullptr;
    if (fixed_actor) {
      strategy = policy_strategy(*fixed, key, action_count, fixed_policy_view);
      if (failure_ != HuPreflopError::InvalidConfiguration) {
        return 0.0;
      }
    } else {
      information = &training.touch(key, action_count, iteration);
      strategy = training.current_strategy(*information);
      const auto average_pass = response_mode ? ExternalSamplingAveragePass::OneSidedTraverser
                                              : ExternalSamplingAveragePass::OpponentOfTraverser;
      const auto average_multiplier =
          sampled_update_weight(options_.sampling_algorithm, iteration) *
          external_sampling_average_multiplier(actor, traverser, reach[actor], average_pass);
      for (std::size_t action = 0; action < action_count; ++action) {
        information->strategy_sum[action] += average_multiplier * strategy[action];
      }
    }
    if (actor != traverser) {
      const auto selected = sample_action(strategy, action_count, random_);
      const auto next = apply_postflop_action(state, actions.value(), selected);
      if (!next) {
        failure_ = next.error();
        return 0.0;
      }
      auto next_reach = reach;
      next_reach[actor] *= strategy[selected];
      return traverse_postflop(next.value(), deal,
                               next_action_history(history, actions.value().actions[selected]),
                               entry_node_id, training, fixed, traverser, iteration, next_reach,
                               response_mode, fixed_policy_view);
    }

    std::array<double, maximum_actions> action_values{};
    std::array<HuPreflopTelemetryTerminalType, maximum_actions> terminal_types{};
    double node_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
      const auto next = apply_postflop_action(state, actions.value(), action);
      if (!next) {
        failure_ = next.error();
        return 0.0;
      }
      auto next_reach = reach;
      next_reach[actor] *= strategy[action];
      action_values[action] = traverse_postflop(
          next.value(), deal, next_action_history(history, actions.value().actions[action]),
          entry_node_id, training, fixed, traverser, iteration, next_reach, response_mode,
          fixed_policy_view);
      terminal_types[action] = postflop_telemetry_terminal(state, next.value());
      node_value += strategy[action] * action_values[action];
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      information->regrets[action] +=
          sampled_update_weight(options_.sampling_algorithm, iteration) *
          (action_values[action] - node_value);
    }
    if (!response_mode && action_count > 1U) {
      const auto [minimum, maximum] =
          std::minmax_element(action_values.begin(), action_values.begin() + action_count);
      const auto spread = *maximum - *minimum;
      postflop_action_value_spread_total_ += spread;
      postflop_action_value_spread_maximum_ =
          std::max(postflop_action_value_spread_maximum_, spread);
      ++postflop_action_value_spread_samples_;
    }
    if (!response_mode && &training == &blueprint_ &&
        options_.collect_action_conditioned_telemetry) {
      for (std::size_t action = 0U; action < action_count; ++action) {
        observe_action_conditioned_telemetry(
            action_conditioned_telemetry_, entry_node_id, key, state,
            static_cast<std::uint8_t>(action), terminal_types[action], deal, reach,
            action_values[action], node_value, action_conditioned_telemetry_dropped_,
            options_.maximum_action_conditioned_telemetry_entries);
      }
    }
    return node_value;
  }

  static std::size_t root_decision_trace_street_index(const Street street) noexcept {
    return static_cast<std::size_t>(street) - static_cast<std::size_t>(Street::Flop);
  }

  bool observe_root_decision_trace_street(const PublicState &state, Deal &deal,
                                          const std::uint64_t history,
                                          RootDecisionTraceSample &trace) {
    if (state.street == Street::Preflop) {
      failure_ = HuPreflopError::IntegrityFailure;
      return false;
    }
    const auto street_index = root_decision_trace_street_index(state.street);
    if (street_index >= trace.street_reached.size()) {
      failure_ = HuPreflopError::IntegrityFailure;
      return false;
    }
    if (trace.street_reached[street_index]) {
      return true;
    }
    trace.street_reached[street_index] = true;
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      auto player_state = state;
      player_state.player_to_act = player;
      const auto key = postflop_key(player_state, deal, history);
      if (!key) {
        failure_ = key.error();
        return false;
      }
      trace.bucket_seen[player][street_index] = true;
      trace.bucket_keys[player][street_index] = telemetry_bucket_key(key.value());
    }
    return true;
  }

  HuPreflopTelemetryTerminalType
  root_decision_trace_postflop_terminal_type(const PublicState &state) const noexcept {
    if (state.status == HandStatus::Folded) {
      return HuPreflopTelemetryTerminalType::PostflopFold;
    }
    if (state.status == HandStatus::AllInRunout) {
      return should_integrate_postflop_all_in(state)
                 ? HuPreflopTelemetryTerminalType::PostflopAllInExact
                 : HuPreflopTelemetryTerminalType::PostflopAllInSampled;
    }
    return HuPreflopTelemetryTerminalType::PostflopShowdown;
  }

  double play_postflop_for_root_decision_trace(const PublicState &state, Deal &deal,
                                               const std::uint64_t history,
                                               const DcfrTable &policy_co,
                                               const DcfrTable &policy_btn, std::mt19937_64 &random,
                                               const std::array<SolverPolicyView, 2> policy_views,
                                               RootDecisionTraceSample &trace) {
    if (!observe_root_decision_trace_street(state, deal, history, trace)) {
      return 0.0;
    }
    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
        state.status == HandStatus::Showdown) {
      trace.terminal_type = root_decision_trace_postflop_terminal_type(state);
      trace.terminal_street = state.street;
      return terminal_payoff(state, deal, 0U);
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_with_board(state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return play_postflop_for_root_decision_trace(
          advanced.value(), deal,
          mix_history(history, 0x4348'414E'4345'0000ULL +
                                   static_cast<std::uint64_t>(advanced.value().street)),
          policy_co, policy_btn, random, policy_views, trace);
    }
    const auto actions = postflop_actions(state, history);
    const auto key = postflop_key(state, deal, history);
    if (!actions || !key) {
      failure_ = !actions ? actions.error() : key.error();
      return 0.0;
    }
    const auto actor = state.player_to_act;
    const auto &policy = actor == 0U ? policy_co : policy_btn;
    const auto strategy =
        policy_strategy(policy, key.value(), actions.value().action_count, policy_views[actor]);
    if (failure_ != HuPreflopError::InvalidConfiguration) {
      return 0.0;
    }
    const auto selected = sample_action(strategy, actions.value().action_count, random);
    const auto next = apply_postflop_action(state, actions.value(), selected);
    if (!next) {
      failure_ = next.error();
      return 0.0;
    }
    return play_postflop_for_root_decision_trace(
        next.value(), deal, next_action_history(history, actions.value().actions[selected]),
        policy_co, policy_btn, random, policy_views, trace);
  }

  double play_preflop_for_root_decision_trace(const std::uint32_t node_id, Deal &deal,
                                              const DcfrTable &policy_co,
                                              const DcfrTable &policy_btn, std::mt19937_64 &random,
                                              const std::array<SolverPolicyView, 2> policy_views,
                                              RootDecisionTraceSample &trace) {
    if (node_id >= tree_.nodes.size()) {
      failure_ = HuPreflopError::GameFailure;
      return 0.0;
    }
    const auto &node = tree_.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::TerminalFold ||
        node.kind == HuPreflopNodeKind::TerminalAllIn) {
      trace.terminal_type = node.kind == HuPreflopNodeKind::TerminalFold
                                ? HuPreflopTelemetryTerminalType::PreflopFold
                            : options_.preflop_all_in_training_oracle
                                ? HuPreflopTelemetryTerminalType::PreflopAllInExact
                                : HuPreflopTelemetryTerminalType::PreflopAllInSampled;
      trace.terminal_street = Street::Preflop;
      return preflop_terminal_payoff(node, deal, 0U);
    }
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      const auto advanced = advance_with_board(node.state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return play_postflop_for_root_decision_trace(
          advanced.value(), deal, mix_history(postflop_history_seed, node.id), policy_co,
          policy_btn, random, policy_views, trace);
    }
    const auto key = preflop_key(node, deal);
    const auto actor = node.state.player_to_act;
    const auto &policy = actor == 0U ? policy_co : policy_btn;
    const auto strategy = policy_strategy(policy, key, node.edges.size(), policy_views[actor]);
    if (failure_ != HuPreflopError::InvalidConfiguration) {
      return 0.0;
    }
    const auto selected = sample_action(strategy, node.edges.size(), random);
    trace.preflop_continuation.push_back(
        HuPreflopRootDecisionTraceStep{node.id, actor, static_cast<std::uint8_t>(selected)});
    return play_preflop_for_root_decision_trace(node.edges[selected].child, deal, policy_co,
                                                policy_btn, random, policy_views, trace);
  }

  Result<HuPreflopRootDecisionTracePolicy, HuPreflopError> evaluate_root_decision_trace_policy(
      const HandClassId hand_class_id, const HuPreflopRootDecisionTracePolicyView public_view,
      const std::uint32_t deals_per_action, const std::uint64_t seed) {
    struct Accumulators {
      std::array<RootDecisionTraceMoments, 5> actions{};
      std::array<std::map<RootDecisionTraceBranchKey, RootDecisionTraceMoments>, 5> branches;
      std::array<std::array<RootDecisionTraceMoments, 3>, 5> streets{};
      std::array<std::map<RootDecisionTraceBucketKey, RootDecisionTraceMoments>, 5> buckets;
      std::array<std::array<RootDecisionTraceMoments, 5>, 5> paired_differences{};
    } accumulators;

    const auto &root = tree_.nodes[tree_.root];
    if (root.kind != HuPreflopNodeKind::Decision || root.edges.size() != 5U ||
        hand_class_id >= hu_preflop_hand_class_count || deals_per_action == 0U) {
      return Result<HuPreflopRootDecisionTracePolicy, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
    std::array<std::size_t, 5> destinations{};
    std::array<bool, 5> destination_seen{};
    for (std::size_t source = 0U; source < root.edges.size(); ++source) {
      const auto destination = root_action_destination(root.edges[source].action);
      if (destination >= destination_seen.size() || destination_seen[destination]) {
        return Result<HuPreflopRootDecisionTracePolicy, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      destinations[source] = destination;
      destination_seen[destination] = true;
    }

    const auto view = public_view == HuPreflopRootDecisionTracePolicyView::Average
                          ? SolverPolicyView::Average
                          : SolverPolicyView::Current;
    const std::array policy_views{view, view};
    std::mt19937_64 deal_random(seed);
    std::mt19937_64 continuation_seed_random(seed ^ 0x4341'5553'414C'0001ULL);
    for (std::uint32_t sample = 0U; sample < deals_per_action; ++sample) {
      const auto conditional = sample_deal_conditioned_on_class(hand_class_id, 0U, deal_random);
      if (!conditional) {
        return Result<HuPreflopRootDecisionTracePolicy, HuPreflopError>::failure(
            conditional.error());
      }
      const auto continuation_seed = continuation_seed_random();
      std::array<double, 5> payoffs{};
      for (std::size_t source = 0U; source < root.edges.size(); ++source) {
        const auto destination = destinations[source];
        auto deal = conditional.value();
        std::mt19937_64 action_random(continuation_seed);
        RootDecisionTraceSample sample_trace;
        sample_trace.preflop_continuation.reserve(8U);
        const auto payoff = play_preflop_for_root_decision_trace(
            root.edges[source].child, deal, blueprint_, blueprint_, action_random, policy_views,
            sample_trace);
        if (failure_ != HuPreflopError::InvalidConfiguration ||
            sample_trace.terminal_type == HuPreflopTelemetryTerminalType::PostflopContinuation) {
          return Result<HuPreflopRootDecisionTracePolicy, HuPreflopError>::failure(
              failure_ != HuPreflopError::InvalidConfiguration ? failure_
                                                               : HuPreflopError::IntegrityFailure);
        }
        payoffs[destination] = payoff;
        accumulators.actions[destination].observe(payoff);
        accumulators
            .branches[destination][RootDecisionTraceBranchKey{sample_trace.preflop_continuation,
                                                              sample_trace.terminal_type,
                                                              sample_trace.terminal_street}]
            .observe(payoff);
        for (std::size_t street = 0U; street < sample_trace.street_reached.size(); ++street) {
          if (sample_trace.street_reached[street]) {
            accumulators.streets[destination][street].observe(payoff);
          }
          for (std::uint8_t player = 0U; player < 2U; ++player) {
            if (!sample_trace.bucket_seen[player][street]) {
              continue;
            }
            const auto street_id =
                static_cast<Street>(static_cast<std::size_t>(Street::Flop) + street);
            accumulators
                .buckets[destination][RootDecisionTraceBucketKey{
                    street_id, player, sample_trace.bucket_keys[player][street]}]
                .observe(payoff);
          }
        }
      }
      for (std::size_t left = 0U; left < payoffs.size(); ++left) {
        for (std::size_t right = 0U; right < payoffs.size(); ++right) {
          accumulators.paired_differences[left][right].observe(payoffs[left] - payoffs[right]);
        }
      }
    }

    HuPreflopRootDecisionTracePolicy output;
    output.policy_view = public_view;
    for (std::size_t action = 0U; action < output.actions.size(); ++action) {
      auto &action_output = output.actions[action];
      action_output.action_id = static_cast<std::uint8_t>(action);
      const auto action_samples = accumulators.actions[action].samples;
      action_output.value = accumulators.actions[action].summarize(action_samples);
      action_output.preflop_branches.reserve(accumulators.branches[action].size());
      for (const auto &[key, moments] : accumulators.branches[action]) {
        action_output.preflop_branches.push_back(HuPreflopRootDecisionTraceBranch{
            key.preflop_continuation, key.terminal_type, key.terminal_street,
            moments.summarize(action_samples)});
      }
      for (std::size_t street = 0U; street < accumulators.streets[action].size(); ++street) {
        const auto &moments = accumulators.streets[action][street];
        if (moments.samples == 0U) {
          continue;
        }
        action_output.street_reach.push_back(HuPreflopRootDecisionTraceStreet{
            static_cast<Street>(static_cast<std::size_t>(Street::Flop) + street),
            moments.summarize(action_samples)});
      }
      action_output.buckets.reserve(accumulators.buckets[action].size());
      for (const auto &[key, moments] : accumulators.buckets[action]) {
        action_output.buckets.push_back(HuPreflopRootDecisionTraceBucket{
            key.street, key.player, key.bucket_key, moments.summarize(action_samples)});
      }
    }
    for (std::size_t left = 0U; left < output.actions.size(); ++left) {
      for (std::size_t right = 0U; right < output.actions.size(); ++right) {
        const auto summary =
            accumulators.paired_differences[left][right].summarize(deals_per_action);
        output.paired_difference_mean_ante[left][right] = summary.mean_payoff_ante;
        output.paired_difference_standard_error_ante[left][right] = summary.standard_error_ante;
      }
    }
    return Result<HuPreflopRootDecisionTracePolicy, HuPreflopError>::success(std::move(output));
  }

  Result<std::vector<HuPreflopRootDecisionTrace>, HuPreflopError> evaluate_root_decision_traces() {
    try {
      std::vector<HuPreflopRootDecisionTrace> output;
      output.reserve(options_.root_decision_trace_hand_classes.size());
      for (const auto hand_class_id : options_.root_decision_trace_hand_classes) {
        HuPreflopRootDecisionTrace trace;
        trace.hand_class = hand_class_id;
        trace.deals_per_action = options_.root_decision_trace_deals_per_class;
        const auto trace_seed = (options_.evaluation_seed ^ 0x5452'4143'4500'0001ULL ^
                                 (static_cast<std::uint64_t>(hand_class_id) << 32U)) |
                                1U;
        const auto average = evaluate_root_decision_trace_policy(
            hand_class_id, HuPreflopRootDecisionTracePolicyView::Average,
            options_.root_decision_trace_deals_per_class, trace_seed);
        if (!average) {
          return Result<std::vector<HuPreflopRootDecisionTrace>, HuPreflopError>::failure(
              average.error());
        }
        trace.policies.push_back(std::move(average.value()));
        if (options_.evaluate_current_profile) {
          const auto current = evaluate_root_decision_trace_policy(
              hand_class_id, HuPreflopRootDecisionTracePolicyView::Current,
              options_.root_decision_trace_deals_per_class, trace_seed);
          if (!current) {
            return Result<std::vector<HuPreflopRootDecisionTrace>, HuPreflopError>::failure(
                current.error());
          }
          trace.policies.push_back(std::move(current.value()));
        }
        output.push_back(std::move(trace));
      }
      return Result<std::vector<HuPreflopRootDecisionTrace>, HuPreflopError>::success(
          std::move(output));
    } catch (const std::bad_alloc &) {
      return Result<std::vector<HuPreflopRootDecisionTrace>, HuPreflopError>::failure(
          HuPreflopError::MemoryFailure);
    }
  }

  double play_preflop(const std::uint32_t node_id, Deal &deal, const DcfrTable &policy_co,
                      const DcfrTable &policy_btn, std::mt19937_64 &random,
                      const std::uint8_t payoff_player = 0U,
                      const std::array<SolverPolicyView, 2> policy_views = {}) {
    const auto &node = tree_.nodes[node_id];
    if (node.kind == HuPreflopNodeKind::TerminalFold ||
        node.kind == HuPreflopNodeKind::TerminalAllIn) {
      return preflop_terminal_payoff(node, deal, payoff_player);
    }
    if (node.kind == HuPreflopNodeKind::PostflopEntry) {
      const auto advanced = advance_with_board(node.state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return play_postflop(advanced.value(), deal, mix_history(postflop_history_seed, node.id),
                           policy_co, policy_btn, random, payoff_player, policy_views);
    }
    const auto key = preflop_key(node, deal);
    const auto actor = node.state.player_to_act;
    const auto &policy = actor == 0U ? policy_co : policy_btn;
    const auto strategy = policy_strategy(policy, key, node.edges.size(), policy_views[actor]);
    if (failure_ != HuPreflopError::InvalidConfiguration) {
      return 0.0;
    }
    const auto selected = sample_action(strategy, node.edges.size(), random);
    return play_preflop(node.edges[selected].child, deal, policy_co, policy_btn, random,
                        payoff_player, policy_views);
  }

  double play_postflop(const PublicState &state, Deal &deal, const std::uint64_t history,
                       const DcfrTable &policy_co, const DcfrTable &policy_btn,
                       std::mt19937_64 &random, const std::uint8_t payoff_player,
                       const std::array<SolverPolicyView, 2> policy_views = {}) {
    if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
        state.status == HandStatus::Showdown) {
      return terminal_payoff(state, deal, payoff_player);
    }
    if (state.status == HandStatus::StreetComplete) {
      const auto advanced = advance_with_board(state, deal);
      if (!advanced) {
        failure_ = advanced.error();
        return 0.0;
      }
      return play_postflop(
          advanced.value(), deal,
          mix_history(history, 0x4348'414E'4345'0000ULL +
                                   static_cast<std::uint64_t>(advanced.value().street)),
          policy_co, policy_btn, random, payoff_player, policy_views);
    }
    const auto actions = postflop_actions(state, history);
    const auto key = postflop_key(state, deal, history);
    if (!actions || !key) {
      failure_ = !actions ? actions.error() : key.error();
      return 0.0;
    }
    const auto actor = state.player_to_act;
    const auto &policy = actor == 0U ? policy_co : policy_btn;
    const auto strategy =
        policy_strategy(policy, key.value(), actions.value().action_count, policy_views[actor]);
    if (failure_ != HuPreflopError::InvalidConfiguration) {
      return 0.0;
    }
    const auto selected = sample_action(strategy, actions.value().action_count, random);
    const auto next = apply_postflop_action(state, actions.value(), selected);
    if (!next) {
      failure_ = next.error();
      return 0.0;
    }
    return play_postflop(next.value(), deal,
                         next_action_history(history, actions.value().actions[selected]), policy_co,
                         policy_btn, random, payoff_player, policy_views);
  }

  Result<EvaluationSummary, HuPreflopError>
  evaluate(const DcfrTable &policy_co, const DcfrTable &policy_btn, const std::uint64_t deals,
           const std::uint64_t seed, const std::array<SolverPolicyView, 2> policy_views = {}) {
    std::mt19937_64 evaluation_random(seed);
    double total = 0.0;
    double squared_total = 0.0;
    for (std::uint64_t index = 0; index < deals; ++index) {
      auto deal = sample_deal(evaluation_random);
      const auto payoff = play_preflop(tree_.root, deal, policy_co, policy_btn, evaluation_random,
                                       0U, policy_views);
      if (failure_ != HuPreflopError::InvalidConfiguration) {
        return Result<EvaluationSummary, HuPreflopError>::failure(failure_);
      }
      total += payoff;
      squared_total += payoff * payoff;
    }
    EvaluationSummary result;
    result.mean = total / static_cast<double>(deals);
    const auto variance =
        std::max(0.0, squared_total / static_cast<double>(deals) - result.mean * result.mean);
    result.standard_error = std::sqrt(variance / static_cast<double>(deals));
    return Result<EvaluationSummary, HuPreflopError>::success(result);
  }

  [[nodiscard]] std::size_t root_action_destination(const Action &action) const {
    const auto &root = tree_.nodes[tree_.root];
    const auto target = root.state.committed_this_street[0].units() + action.amount.units();
    if (action.type == ActionType::AllIn) {
      return 0U;
    }
    if (action.type == ActionType::Raise && target == tree_.config.open_targets[0].units()) {
      return 1U;
    }
    if (action.type == ActionType::Raise && target == tree_.config.open_targets[1].units()) {
      return 2U;
    }
    if (action.type == ActionType::Call) {
      return 3U;
    }
    return action.type == ActionType::Fold ? 4U : 5U;
  }

  bool
  observe_root_action_advantage(const HandClassId class_id,
                                const std::array<double, maximum_actions> &source_action_advantage,
                                const std::size_t action_count, const std::uint64_t iteration) {
    if (class_id >= root_action_advantage_accumulators_.size() || action_count != 5U) {
      failure_ = HuPreflopError::IntegrityFailure;
      return false;
    }
    std::array<double, 5> stable_action_advantage{};
    const auto &root = tree_.nodes[tree_.root];
    for (std::size_t source = 0U; source < action_count; ++source) {
      const auto destination = root_action_destination(root.edges[source].action);
      if (destination >= stable_action_advantage.size()) {
        failure_ = HuPreflopError::IntegrityFailure;
        return false;
      }
      stable_action_advantage[destination] = source_action_advantage[source];
    }

    const auto weight = sampled_update_weight(options_.sampling_algorithm, iteration);
    auto &accumulator = root_action_advantage_accumulators_[class_id];
    const auto updated_weight_sum = accumulator.weight_sum + weight;
    std::array<double, 5> delta{};
    std::array<double, 5> updated_mean{};
    for (std::size_t action = 0U; action < stable_action_advantage.size(); ++action) {
      delta[action] = stable_action_advantage[action] - accumulator.online_mean[action];
      updated_mean[action] =
          accumulator.online_mean[action] + weight / updated_weight_sum * delta[action];
      accumulator.weighted_sum[action] += weight * stable_action_advantage[action];
    }
    for (std::size_t left = 0U; left < stable_action_advantage.size(); ++left) {
      for (std::size_t right = 0U; right < stable_action_advantage.size(); ++right) {
        accumulator.co_moment[left][right] +=
            weight * delta[left] * (stable_action_advantage[right] - updated_mean[right]);
      }
    }
    accumulator.online_mean = updated_mean;
    accumulator.weight_sum = updated_weight_sum;
    accumulator.squared_weight_sum += weight * weight;
    ++accumulator.observations;
    return true;
  }

  bool observe_root_action_advantage(const HandClassId class_id,
                                     const std::array<double, maximum_actions> &action_values,
                                     const std::size_t action_count, const std::uint64_t iteration,
                                     const double node_value) {
    std::array<double, maximum_actions> source_action_advantage{};
    for (std::size_t action = 0U; action < action_count; ++action) {
      source_action_advantage[action] = action_values[action] - node_value;
    }
    return observe_root_action_advantage(class_id, source_action_advantage, action_count,
                                         iteration);
  }

  Result<RootActionEvaluation, HuPreflopError>
  evaluate_root_actions(const DcfrTable &policy_co, const DcfrTable &policy_btn,
                        const std::uint64_t deals, const std::uint64_t seed,
                        const std::array<SolverPolicyView, 2> policy_views = {}) {
    const auto &root = tree_.nodes[tree_.root];
    if (root.kind != HuPreflopNodeKind::Decision || root.edges.size() != 5U) {
      return Result<RootActionEvaluation, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }

    std::array<std::size_t, 5> destinations{};
    std::array<bool, 5> destination_seen{};
    for (std::size_t source = 0U; source < root.edges.size(); ++source) {
      const auto destination = root_action_destination(root.edges[source].action);
      if (destination >= destination_seen.size() || destination_seen[destination]) {
        return Result<RootActionEvaluation, HuPreflopError>::failure(
            HuPreflopError::InvalidConfiguration);
      }
      destinations[source] = destination;
      destination_seen[destination] = true;
    }
    if (!std::all_of(destination_seen.begin(), destination_seen.end(),
                     [](const bool seen) { return seen; })) {
      return Result<RootActionEvaluation, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }

    std::mt19937_64 deal_random(seed);
    std::array<std::mt19937_64, 5> action_randoms{};
    auto action_seed = seed ^ 0x4143'5449'4F4E'4556ULL;
    for (auto &action_random : action_randoms) {
      action_random.seed(splitmix64(action_seed));
    }
    std::array<std::array<double, 5>, 81> totals{};
    std::array<std::array<double, 5>, 81> squared_totals{};
    RootActionEvaluation result;
    for (std::uint64_t index = 0U; index < deals; ++index) {
      const auto sampled_deal = sample_deal(deal_random);
      const auto class_id = deal_class(sampled_deal, 0U);
      for (std::size_t source = 0U; source < root.edges.size(); ++source) {
        const auto destination = destinations[source];
        auto action_deal = sampled_deal;
        const auto payoff = play_preflop(root.edges[source].child, action_deal, policy_co,
                                         policy_btn, action_randoms[destination], 0U, policy_views);
        if (failure_ != HuPreflopError::InvalidConfiguration) {
          return Result<RootActionEvaluation, HuPreflopError>::failure(failure_);
        }
        totals[class_id][destination] += payoff;
        squared_totals[class_id][destination] += payoff * payoff;
        ++result.samples[class_id][destination];
      }
    }
    for (std::size_t class_id = 0U; class_id < result.samples.size(); ++class_id) {
      for (std::size_t action = 0U; action < result.samples[class_id].size(); ++action) {
        const auto samples = result.samples[class_id][action];
        if (samples == 0U) {
          continue;
        }
        const auto denominator = static_cast<double>(samples);
        result.means[class_id][action] = totals[class_id][action] / denominator;
        const auto variance =
            std::max(0.0, squared_totals[class_id][action] / denominator -
                              result.means[class_id][action] * result.means[class_id][action]);
        result.standard_errors[class_id][action] = std::sqrt(variance / denominator);
      }
    }
    return Result<RootActionEvaluation, HuPreflopError>::success(std::move(result));
  }

  struct PreflopPathStep {
    std::uint32_t node_id{0U};
    std::size_t edge_index{0U};
  };

  bool find_preflop_path(const std::uint32_t current, const std::uint32_t target,
                         std::vector<PreflopPathStep> &path) const {
    if (current == target) {
      return true;
    }
    if (current >= tree_.nodes.size()) {
      return false;
    }
    const auto &node = tree_.nodes[current];
    for (std::size_t edge = 0U; edge < node.edges.size(); ++edge) {
      path.push_back(PreflopPathStep{current, edge});
      if (find_preflop_path(node.edges[edge].child, target, path)) {
        return true;
      }
      path.pop_back();
    }
    return false;
  }

  Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>
  evaluate_preflop_decisions(const DcfrTable &policy_co, const DcfrTable &policy_btn,
                             const std::uint64_t deals, const std::uint64_t seed,
                             const RootActionEvaluation &root_evaluation,
                             const std::array<SolverPolicyView, 2> policy_views = {}) {
    struct WeightedAccumulator {
      double weighted_total{0.0};
      double weighted_squared_total{0.0};
      double weight_total{0.0};
      double squared_weight_total{0.0};
      std::uint64_t samples{0U};
    };

    if (deals == 0U) {
      return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
    std::vector<HuPreflopDecisionEvaluation> output;
    output.reserve(static_cast<std::size_t>(tree_.stats.decision_nodes));
    for (const auto &node : tree_.nodes) {
      if (node.kind != HuPreflopNodeKind::Decision) {
        continue;
      }
      if (node.state.player_to_act > 1U || node.edges.empty() ||
          node.edges.size() > hu_preflop_maximum_actions) {
        return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
            HuPreflopError::InvalidConfiguration);
      }

      HuPreflopDecisionEvaluation evaluated;
      evaluated.node_id = node.id;
      evaluated.player = node.state.player_to_act;
      evaluated.action_count = static_cast<std::uint8_t>(node.edges.size());
      if (node.id == tree_.root) {
        for (std::size_t source = 0U; source < node.edges.size(); ++source) {
          const auto destination = root_action_destination(node.edges[source].action);
          for (std::size_t class_id = 0U; class_id < hu_preflop_hand_class_count; ++class_id) {
            evaluated.action_ev_ante[class_id][source] =
                root_evaluation.means[class_id][destination];
            evaluated.action_ev_standard_error_ante[class_id][source] =
                root_evaluation.standard_errors[class_id][destination];
            evaluated.action_ev_samples[class_id][source] =
                root_evaluation.samples[class_id][destination];
            evaluated.action_ev_effective_samples[class_id][source] =
                static_cast<double>(root_evaluation.samples[class_id][destination]);
          }
        }
        output.push_back(std::move(evaluated));
        continue;
      }

      std::vector<PreflopPathStep> path;
      if (!find_preflop_path(tree_.root, node.id, path)) {
        return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
            HuPreflopError::GameFailure);
      }
      std::array<std::array<WeightedAccumulator, hu_preflop_maximum_actions>,
                 hu_preflop_hand_class_count>
          accumulators{};
      auto node_seed =
          seed ^ (static_cast<std::uint64_t>(node.id) << 32U) ^ 0x4E4F'4445'4556'0001ULL;
      std::mt19937_64 deal_random(splitmix64(node_seed));
      std::array<std::mt19937_64, hu_preflop_maximum_actions> action_randoms{};
      for (std::size_t action = 0U; action < node.edges.size(); ++action) {
        action_randoms[action].seed(splitmix64(node_seed));
      }

      for (std::uint64_t index = 0U; index < deals; ++index) {
        const auto sampled_deal = sample_deal(deal_random);
        const auto class_id = static_cast<std::size_t>(deal_class(sampled_deal, evaluated.player));
        double opponent_reach = 1.0;
        for (const auto &step : path) {
          const auto &prior = tree_.nodes[step.node_id];
          if (prior.kind != HuPreflopNodeKind::Decision || step.edge_index >= prior.edges.size()) {
            return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
                HuPreflopError::GameFailure);
          }
          if (prior.state.player_to_act == evaluated.player) {
            continue;
          }
          const auto &policy = prior.state.player_to_act == 0U ? policy_co : policy_btn;
          const auto strategy =
              policy_strategy(policy, preflop_key(prior, sampled_deal), prior.edges.size(),
                              policy_views[prior.state.player_to_act]);
          if (failure_ != HuPreflopError::InvalidConfiguration) {
            return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
                failure_);
          }
          opponent_reach *= strategy[step.edge_index];
        }
        if (!(opponent_reach > 0.0) || !std::isfinite(opponent_reach)) {
          if (opponent_reach < 0.0 || !std::isfinite(opponent_reach)) {
            return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
                HuPreflopError::NumericalFailure);
          }
          continue;
        }

        for (std::size_t action = 0U; action < node.edges.size(); ++action) {
          auto action_deal = sampled_deal;
          const auto payoff =
              play_preflop(node.edges[action].child, action_deal, policy_co, policy_btn,
                           action_randoms[action], evaluated.player, policy_views);
          if (failure_ != HuPreflopError::InvalidConfiguration || !std::isfinite(payoff)) {
            return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
                failure_ != HuPreflopError::InvalidConfiguration
                    ? failure_
                    : HuPreflopError::NumericalFailure);
          }
          auto &accumulator = accumulators[class_id][action];
          accumulator.weighted_total += opponent_reach * payoff;
          accumulator.weighted_squared_total += opponent_reach * payoff * payoff;
          accumulator.weight_total += opponent_reach;
          accumulator.squared_weight_total += opponent_reach * opponent_reach;
          ++accumulator.samples;
        }
      }

      for (std::size_t class_id = 0U; class_id < hu_preflop_hand_class_count; ++class_id) {
        for (std::size_t action = 0U; action < node.edges.size(); ++action) {
          const auto &accumulator = accumulators[class_id][action];
          if (!(accumulator.weight_total > 0.0) || !(accumulator.squared_weight_total > 0.0)) {
            continue;
          }
          const auto mean = accumulator.weighted_total / accumulator.weight_total;
          const auto variance = std::max(
              0.0, accumulator.weighted_squared_total / accumulator.weight_total - mean * mean);
          const auto effective_samples = accumulator.weight_total * accumulator.weight_total /
                                         accumulator.squared_weight_total;
          evaluated.action_ev_ante[class_id][action] = mean;
          evaluated.action_ev_standard_error_ante[class_id][action] =
              std::sqrt(variance / effective_samples);
          evaluated.action_ev_samples[class_id][action] = accumulator.samples;
          evaluated.action_ev_effective_samples[class_id][action] = effective_samples;
        }
      }
      output.push_back(std::move(evaluated));
    }
    if (output.size() != tree_.stats.decision_nodes) {
      return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::failure(
          HuPreflopError::GameFailure);
    }
    return Result<std::vector<HuPreflopDecisionEvaluation>, HuPreflopError>::success(
        std::move(output));
  }

  void train_response(const std::uint8_t player, DcfrTable &response, const std::uint64_t seed,
                      const SolverPolicyView fixed_policy_view = SolverPolicyView::Average) {
    random_.seed(seed);
    for (std::uint64_t iteration = 1U; iteration <= options_.best_response_iterations;
         ++iteration) {
      auto deal = sample_deal(random_);
      const std::array<double, 2> reach{1.0, 1.0};
      (void)traverse_preflop(tree_.root, deal, response, &blueprint_, player, iteration, reach,
                             true, fixed_policy_view);
      if (failure_ != HuPreflopError::InvalidConfiguration) {
        return;
      }
    }
  }

  void extract_root_strategy(std::array<std::array<double, 5>, 81> &output) const {
    const auto &root = tree_.nodes[tree_.root];
    for (std::uint8_t class_id = 0; class_id < 81U; ++class_id) {
      InformationKey key;
      key.public_history = root.id;
      key.preflop_class = class_id;
      key.player = root.state.player_to_act;
      const auto strategy = blueprint_.average_strategy(key, root.edges.size());
      for (std::size_t source = 0; source < root.edges.size(); ++source) {
        const auto destination = root_action_destination(root.edges[source].action);
        output[class_id][destination] = strategy[source];
      }
    }
  }

  void extract_root_training_diagnostics(HuPreflopSolveResult &output) const {
    const auto &root = tree_.nodes[tree_.root];
    for (std::uint8_t class_id = 0U; class_id < hu_preflop_hand_class_count; ++class_id) {
      InformationKey key;
      key.public_history = root.id;
      key.preflop_class = class_id;
      key.player = root.state.player_to_act;
      const auto *information = blueprint_.find(key, root.edges.size());
      std::array<double, maximum_actions> current{};
      if (information == nullptr) {
        std::fill_n(current.begin(), root.edges.size(),
                    1.0 / static_cast<double>(root.edges.size()));
      } else {
        current = blueprint_.current_strategy(*information);
        output.root_information_last_iteration[class_id] = information->last_iteration;
      }
      for (std::size_t source = 0U; source < root.edges.size(); ++source) {
        const auto destination = root_action_destination(root.edges[source].action);
        output.root_current_strategy[class_id][destination] = current[source];
        if (information != nullptr) {
          output.root_cumulative_weighted_regret[class_id][destination] =
              information->regrets[source];
          output.root_cumulative_average_weight[class_id][destination] =
              information->strategy_sum[source];
        }
      }

      const auto &accumulator = root_action_advantage_accumulators_[class_id];
      auto &diagnostic = output.root_action_advantage_diagnostics[class_id];
      diagnostic.observations = accumulator.observations;
      diagnostic.weight_sum = accumulator.weight_sum;
      if (!(accumulator.weight_sum > 0.0) || !(accumulator.squared_weight_sum > 0.0)) {
        continue;
      }
      diagnostic.effective_samples =
          accumulator.weight_sum * accumulator.weight_sum / accumulator.squared_weight_sum;
      const auto reliability_denominator =
          accumulator.weight_sum - accumulator.squared_weight_sum / accumulator.weight_sum;
      std::array<std::array<double, 5>, 5> covariance{};
      for (std::size_t action = 0U; action < diagnostic.weighted_mean_ante.size(); ++action) {
        diagnostic.weighted_mean_ante[action] =
            accumulator.weighted_sum[action] / accumulator.weight_sum;
        diagnostic.cumulative_regret_reconstruction_error_ante[action] =
            accumulator.weighted_sum[action] -
            output.root_cumulative_weighted_regret[class_id][action];
        if (reliability_denominator > 0.0) {
          covariance[action][action] =
              std::max(0.0, accumulator.co_moment[action][action] / reliability_denominator);
          diagnostic.weighted_standard_deviation_ante[action] =
              std::sqrt(covariance[action][action]);
          diagnostic.weighted_standard_error_ante[action] =
              std::sqrt(covariance[action][action] / diagnostic.effective_samples);
        }
      }
      if (reliability_denominator > 0.0) {
        for (std::size_t left = 0U; left < covariance.size(); ++left) {
          for (std::size_t right = 0U; right < covariance.size(); ++right) {
            covariance[left][right] = accumulator.co_moment[left][right] / reliability_denominator;
          }
        }
      }
      for (std::size_t left = 0U; left < diagnostic.pairwise_weighted_mean_ante.size(); ++left) {
        for (std::size_t right = left; right < diagnostic.pairwise_weighted_mean_ante.size();
             ++right) {
          const auto difference =
              diagnostic.weighted_mean_ante[left] - diagnostic.weighted_mean_ante[right];
          const auto variance =
              std::max(0.0, covariance[left][left] + covariance[right][right] -
                                covariance[left][right] - covariance[right][left]);
          const auto standard_error = diagnostic.effective_samples > 0.0
                                          ? std::sqrt(variance / diagnostic.effective_samples)
                                          : 0.0;
          diagnostic.pairwise_weighted_mean_ante[left][right] = difference;
          diagnostic.pairwise_weighted_mean_ante[right][left] = -difference;
          diagnostic.pairwise_weighted_standard_error_ante[left][right] = standard_error;
          diagnostic.pairwise_weighted_standard_error_ante[right][left] = standard_error;
        }
      }
    }
  }

  void extract_preflop_training_diagnostics(
      std::vector<HuPreflopDecisionTrainingDiagnostic> &output) const {
    output.clear();
    output.reserve(static_cast<std::size_t>(tree_.stats.decision_nodes));
    for (const auto &node : tree_.nodes) {
      if (node.kind != HuPreflopNodeKind::Decision) {
        continue;
      }
      HuPreflopDecisionTrainingDiagnostic diagnostic;
      diagnostic.node_id = node.id;
      diagnostic.player = node.state.player_to_act;
      diagnostic.action_count = static_cast<std::uint8_t>(node.edges.size());
      for (std::size_t hand = 0U; hand < hu_preflop_hand_class_count; ++hand) {
        InformationKey key;
        key.public_history = node.id;
        key.preflop_class = static_cast<HandClassId>(hand);
        key.player = node.state.player_to_act;
        const auto *information = blueprint_.find(key, node.edges.size());
        if (information == nullptr) {
          std::fill_n(diagnostic.current_strategy[hand].begin(), node.edges.size(),
                      1.0 / static_cast<double>(node.edges.size()));
          continue;
        }
        const auto current = blueprint_.current_strategy(*information);
        std::copy_n(current.begin(), node.edges.size(), diagnostic.current_strategy[hand].begin());
        diagnostic.last_iteration[hand] = information->last_iteration;
        for (std::size_t action = 0U; action < node.edges.size(); ++action) {
          diagnostic.cumulative_weighted_regret[hand][action] = information->regrets[action];
          diagnostic.cumulative_average_weight[hand][action] = information->strategy_sum[action];
        }
      }
      output.push_back(std::move(diagnostic));
    }
  }

  Result<SampledResponseSummary, HuPreflopError>
  evaluate_sampled_response_profile(const SolverPolicyView fixed_policy_view,
                                    const std::uint64_t remaining_states,
                                    const std::uint64_t seed_salt) {
    DcfrTable response_co(options_.best_response_iterations, options_.sampling_algorithm,
                          remaining_states / 2U);
    DcfrTable response_btn(options_.best_response_iterations, options_.sampling_algorithm,
                           remaining_states - remaining_states / 2U);
    train_response(0U, response_co, options_.seed ^ 0x4252'434F'0000'0001ULL ^ seed_salt,
                   fixed_policy_view);
    train_response(1U, response_btn, options_.seed ^ 0x4252'4254'4E00'0001ULL ^ seed_salt,
                   fixed_policy_view);
    if (failure_ != HuPreflopError::InvalidConfiguration || !response_co.valid() ||
        !response_btn.valid()) {
      return Result<SampledResponseSummary, HuPreflopError>::failure(
          failure_ != HuPreflopError::InvalidConfiguration ? failure_
          : !response_co.valid()                           ? response_co.error()
                                                           : response_btn.error());
    }
    const std::array co_views{SolverPolicyView::Average, fixed_policy_view};
    const std::array btn_views{fixed_policy_view, SolverPolicyView::Average};
    const auto br_co =
        evaluate(response_co, blueprint_, options_.best_response_evaluation_deals,
                 options_.evaluation_seed ^ 0x4252'4556'434F'0001ULL ^ seed_salt, co_views);
    const auto br_btn =
        evaluate(blueprint_, response_btn, options_.best_response_evaluation_deals,
                 options_.evaluation_seed ^ 0x4252'4556'4254'4E01ULL ^ seed_salt, btn_views);
    if (!br_co || !br_btn) {
      return Result<SampledResponseSummary, HuPreflopError>::failure(!br_co ? br_co.error()
                                                                            : br_btn.error());
    }
    SampledResponseSummary result;
    result.co = br_co.value();
    result.btn.mean = -br_btn.value().mean;
    result.btn.standard_error = br_btn.value().standard_error;
    result.information_sets = response_co.size() + response_btn.size();
    return Result<SampledResponseSummary, HuPreflopError>::success(std::move(result));
  }

  void extract_preflop_blueprint(HuPreflopBlueprint &output, const std::string &algorithm) const {
    output.tree_fingerprint = tree_.fingerprint;
    output.algorithm = algorithm;
    output.iterations = options_.iterations + options_.preflop_refinement_iterations;
    output.decisions.reserve(static_cast<std::size_t>(tree_.stats.decision_nodes));
    for (const auto &node : tree_.nodes) {
      if (node.kind != HuPreflopNodeKind::Decision ||
          node.edges.size() > hu_preflop_maximum_actions) {
        continue;
      }
      HuPreflopBlueprintDecision decision;
      decision.node_id = node.id;
      decision.player = node.state.player_to_act;
      decision.action_count = static_cast<std::uint8_t>(node.edges.size());
      for (std::uint8_t class_id = 0U; class_id < hu_preflop_hand_class_count; ++class_id) {
        InformationKey key;
        key.public_history = node.id;
        key.preflop_class = class_id;
        key.player = node.state.player_to_act;
        const auto strategy = blueprint_.average_strategy(key, node.edges.size());
        std::copy_n(strategy.begin(), node.edges.size(), decision.strategy[class_id].begin());
      }
      output.decisions.push_back(std::move(decision));
    }
    output.fingerprint = fingerprint_hu_preflop_blueprint(output);
  }

  Result<std::uint64_t, HuPreflopError>
  extract_postflop_policy(HuPreflopSampledPostflopPolicy &output, const std::string &algorithm,
                          const std::string &abstraction_id, const std::uint64_t iterations) const {
    std::uint64_t postflop_entry_count = 0U;
    blueprint_.for_each_state(
        [&postflop_entry_count](const InformationKey &key, const InformationState &) {
          postflop_entry_count += key.street != Street::Preflop ? 1U : 0U;
        });
    if (postflop_entry_count > std::numeric_limits<std::uint64_t>::max() /
                                   sizeof(HuPreflopSampledPostflopPolicyEntry) ||
        postflop_entry_count * sizeof(HuPreflopSampledPostflopPolicyEntry) >
            options_.maximum_exported_postflop_policy_payload_bytes) {
      return Result<std::uint64_t, HuPreflopError>::failure(HuPreflopError::MemoryFailure);
    }
    output.tree_fingerprint = tree_.fingerprint;
    output.major = HuPreflopSampledPostflopPolicy::format_major;
    output.minor =
        options_.postflop_representation == HuPreflopPostflopRepresentation::
                                                DistributionalStrengthStreetAdaptivePerfectRecallV23
            ? 12U
        : options_.evaluate_current_profile ? 11U
        : options_.postflop_representation ==
                HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11
            ? 10U
        : options_.postflop_representation ==
                HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10
            ? 9U
        : options_.postflop_representation ==
                HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9
            ? 8U
        : options_.postflop_representation ==
                HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8
            ? 7U
        : options_.postflop_representation ==
                HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7
            ? 6U
            : HuPreflopSampledPostflopPolicy::minimum_supported_minor;
    output.algorithm = algorithm;
    output.abstraction_id = abstraction_id;
    output.iterations = iterations;
    output.seed = options_.seed;
    output.partition_seed = options_.partition_seed;
    output.distributional_bucket_capacities = options_.distributional_bucket_capacities;
    output.equity_samples_per_bucket =
        options_.postflop_representation == HuPreflopPostflopRepresentation::ExactPhysical
            ? 0U
            : options_.equity_samples_per_bucket;
    output.representation = options_.postflop_representation;
    output.missing_infoset_fallback = HuPreflopSampledPolicyFallback::Uniform;
    output.current_policy_present = options_.evaluate_current_profile;
    output.entries.reserve(static_cast<std::size_t>(postflop_entry_count));
    blueprint_.for_each_state(
        [this, &output](const InformationKey &key, const InformationState &state) {
          if (key.street == Street::Preflop) {
            return;
          }
          HuPreflopSampledPostflopPolicyEntry entry;
          entry.key.public_history = key.public_history;
          entry.key.physical_cards = key.physical_cards;
          entry.key.bucket_history = key.bucket_history;
          entry.key.preflop_class = key.preflop_class;
          entry.key.player = key.player;
          entry.key.street = key.street;
          entry.action_count = state.action_count;
          entry.probabilities = blueprint_.average_strategy(key, state.action_count);
          if (options_.evaluate_current_profile) {
            entry.current_probabilities = blueprint_.current_strategy(state);
          }
          output.entries.push_back(std::move(entry));
        });
    std::ranges::sort(output.entries, [](const auto &left, const auto &right) {
      return sampled_policy_key_less(left.key, right.key);
    });
    output.fingerprint = fingerprint_hu_preflop_sampled_postflop_policy(output);
    return Result<std::uint64_t, HuPreflopError>::success(
        postflop_entry_count * sizeof(HuPreflopSampledPostflopPolicyEntry));
  }

  const HuPreflopTree &tree_;
  const HuPreflopSolveOptions &options_;
  std::mt19937_64 random_;
  DcfrTable blueprint_;
  OpponentValueBaselineTable opponent_value_baselines_;
  ActionConfig postflop_config_;
  std::optional<CompiledBettingPlan> compiled_betting_;
  std::array<std::array<BoundedBucketCache, 3>, 2> bucket_cache_;
  std::array<RootActionAdvantageAccumulator, hu_preflop_hand_class_count>
      root_action_advantage_accumulators_{};
  ActionConditionedTelemetryMap action_conditioned_telemetry_;
  std::uint64_t action_conditioned_telemetry_dropped_{0U};
  std::array<std::uint64_t, 3> bucket_mapping_visits_{};
  std::array<std::uint64_t, 3> bucket_mapping_computations_{};
  std::array<std::bitset<hu_preflop_sampled_postflop_unset_bucket>, 3>
      occupied_distributional_buckets_{};
  double bucket_mapping_seconds_{0.0};
  std::uint64_t postflop_action_value_spread_samples_{0U};
  double postflop_action_value_spread_total_{0.0};
  double postflop_action_value_spread_maximum_{0.0};
  std::uint64_t parallel_bucket_cache_peak_entries_{0U};
  std::uint64_t bucket_cache_evictions_from_parallel_{0U};
  std::uint64_t peak_parallel_updates_per_job_{0U};
  std::uint64_t peak_parallel_shadow_updates_per_worker_{0U};
  std::uint64_t peak_parallel_scratch_payload_bytes_{0U};
  std::optional<SevenCardLookupTable> seven_card_table_;
  std::uint64_t winner_cache_hits_{0U};
  std::uint64_t winner_cache_misses_{0U};
  std::array<std::uint64_t, 2> exact_postflop_all_in_evaluations_{};
  std::array<std::uint64_t, 2> exact_postflop_all_in_runouts_{};
  double exact_postflop_all_in_seconds_{0.0};
  BoundedExactAllInEquityCache exact_postflop_all_in_cache_;
  std::uint64_t exact_postflop_all_in_cache_hits_{0U};
  std::uint64_t exact_postflop_all_in_cache_misses_{0U};
  std::uint64_t parallel_exact_postflop_all_in_cache_peak_entries_{0U};
  std::uint64_t parallel_exact_postflop_all_in_cache_evictions_{0U};
  HuPreflopError failure_{HuPreflopError::InvalidConfiguration};
};

} // namespace

Result<HuPreflopPostflopAllInEquity, HuPreflopError>
enumerate_hu_preflop_postflop_all_in_equity(const std::array<std::array<CardId, 2>, 2> &holes,
                                            const std::array<CardId, 5> &board, const Street street,
                                            const IHandEvaluator &evaluator) {
  const std::size_t visible_board_count = street == Street::Flop    ? 3U
                                          : street == Street::Turn  ? 4U
                                          : street == Street::River ? 5U
                                                                    : 0U;
  if (visible_board_count == 0U) {
    return Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  std::uint64_t dead_mask = 0U;
  const auto add_dead = [&dead_mask](const CardId card) {
    if (card.value() >= 36U || (dead_mask & card.mask()) != 0U) {
      return false;
    }
    dead_mask |= card.mask();
    return true;
  };
  for (const auto &hole : holes) {
    for (const auto card : hole) {
      if (!add_dead(card)) {
        return Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
            HuPreflopError::InvalidConfiguration);
      }
    }
  }
  for (std::size_t index = 0U; index < visible_board_count; ++index) {
    if (!add_dead(board[index])) {
      return Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
  }

  std::array<CardId, 29> available{};
  std::size_t available_count = 0U;
  for (const auto card : short_deck()) {
    if ((dead_mask & card.mask()) == 0U) {
      available[available_count++] = card;
    }
  }
  if (available_count != 36U - 4U - visible_board_count) {
    return Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }

  HuPreflopPostflopAllInEquity result;
  auto complete_board = board;
  const auto evaluate_runout = [&]() {
    const std::array<CardId, 7> first{holes[0][0],       holes[0][1],       complete_board[0],
                                      complete_board[1], complete_board[2], complete_board[3],
                                      complete_board[4]};
    const std::array<CardId, 7> second{holes[1][0],       holes[1][1],       complete_board[0],
                                       complete_board[1], complete_board[2], complete_board[3],
                                       complete_board[4]};
    const auto first_value = evaluator.evaluate_seven(first);
    const auto second_value = evaluator.evaluate_seven(second);
    if (!first_value || !second_value) {
      return false;
    }
    if (first_value.value() > second_value.value()) {
      ++result.wins;
    } else if (first_value.value() == second_value.value()) {
      ++result.ties;
    } else {
      ++result.losses;
    }
    return true;
  };

  if (street == Street::River) {
    if (!evaluate_runout()) {
      return Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
          HuPreflopError::EquityFailure);
    }
  } else if (street == Street::Turn) {
    for (std::size_t river = 0U; river < available_count; ++river) {
      complete_board[4] = available[river];
      if (!evaluate_runout()) {
        return Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
            HuPreflopError::EquityFailure);
      }
    }
  } else {
    for (std::size_t turn = 0U; turn + 1U < available_count; ++turn) {
      complete_board[3] = available[turn];
      for (std::size_t river = turn + 1U; river < available_count; ++river) {
        complete_board[4] = available[river];
        if (!evaluate_runout()) {
          return Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
              HuPreflopError::EquityFailure);
        }
      }
    }
  }

  const auto expected_runouts = street == Street::Flop ? 406U : street == Street::Turn ? 28U : 1U;
  return result.runouts() == expected_runouts
             ? Result<HuPreflopPostflopAllInEquity, HuPreflopError>::success(result)
             : Result<HuPreflopPostflopAllInEquity, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<HuPreflopPostflopAllInEquity, HuPreflopError>
enumerate_hu_preflop_postflop_all_in_equity(const std::array<std::array<CardId, 2>, 2> &holes,
                                            const std::array<CardId, 5> &board,
                                            const Street street) {
  static const ExactHandEvaluator evaluator;
  return enumerate_hu_preflop_postflop_all_in_equity(holes, board, street, evaluator);
}

Result<bool, HuPreflopError>
validate_hu_preflop_all_in_training_oracle(const HuPreflopAllInTrainingOracle &oracle) {
  if (oracle.source_equity_table_fingerprint.empty()) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  for (const auto &matchup : oracle.matchups) {
    const auto loss_probability = 1.0 - matchup.win_probability - matchup.tie_probability;
    if (!std::isfinite(matchup.win_probability) || !std::isfinite(matchup.tie_probability) ||
        !std::isfinite(loss_probability) || matchup.win_probability < 0.0 ||
        matchup.tie_probability < 0.0 || loss_probability < -1.0e-12 ||
        matchup.win_probability > 1.0 || matchup.tie_probability > 1.0) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

std::string
fingerprint_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy) {
  auto hash = policy_fingerprint_offset;
  mix_policy_fingerprint_unsigned(hash, policy.major);
  mix_policy_fingerprint_unsigned(hash, policy.minor);
  mix_policy_fingerprint_string(hash, policy.tree_fingerprint);
  mix_policy_fingerprint_string(hash, policy.algorithm);
  mix_policy_fingerprint_string(hash, policy.abstraction_id);
  mix_policy_fingerprint_unsigned(hash, policy.iterations);
  mix_policy_fingerprint_unsigned(hash, policy.seed);
  mix_policy_fingerprint_unsigned(hash, policy.partition_seed);
  mix_policy_fingerprint_unsigned(hash, policy.equity_samples_per_bucket);
  for (const auto capacity : policy.distributional_bucket_capacities) {
    mix_policy_fingerprint_unsigned(hash, capacity);
  }
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(policy.representation));
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(policy.missing_infoset_fallback));
  if (policy.minor >= 11U) {
    mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(policy.current_policy_present));
  }
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint64_t>(policy.entries.size()));
  for (const auto &entry : policy.entries) {
    mix_policy_fingerprint_unsigned(hash, entry.key.public_history);
    mix_policy_fingerprint_unsigned(hash, entry.key.physical_cards);
    for (const auto bucket : entry.key.bucket_history) {
      mix_policy_fingerprint_unsigned(hash, bucket);
    }
    mix_policy_fingerprint_unsigned(hash, entry.key.preflop_class);
    mix_policy_fingerprint_unsigned(hash, entry.key.player);
    mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(entry.key.street));
    mix_policy_fingerprint_unsigned(hash, entry.action_count);
    for (const auto probability : entry.probabilities) {
      mix_policy_fingerprint_double(hash, probability);
    }
    if (policy.minor >= 11U) {
      for (const auto probability : entry.current_probabilities) {
        mix_policy_fingerprint_double(hash, probability);
      }
    }
  }
  return finish_policy_fingerprint(hash);
}

Result<bool, HuPreflopError>
validate_hu_preflop_sampled_postflop_policy(const HuPreflopTree &tree,
                                            const HuPreflopSampledPostflopPolicy &policy) {
  if (!supported_sampled_policy_version(policy) || policy.tree_fingerprint != tree.fingerprint ||
      policy.algorithm.empty() || policy.abstraction_id.empty() || policy.iterations == 0U ||
      policy.partition_seed == 0U ||
      std::ranges::any_of(policy.distributional_bucket_capacities,
                          [](const auto capacity) {
                            return capacity < 2U || capacity > 32'768U ||
                                   !std::has_single_bit(capacity);
                          }) ||
      static_cast<std::uint8_t>(policy.representation) >
          static_cast<std::uint8_t>(HuPreflopPostflopRepresentation::
                                        DistributionalStrengthStreetAdaptivePerfectRecallV23) ||
      ((policy.representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
        policy.representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 ||
        policy.representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9 ||
        policy.representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10 ||
        policy.representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11 ||
        policy.representation == HuPreflopPostflopRepresentation::
                                     DistributionalStrengthStreetAdaptivePerfectRecallV23) &&
       std::ranges::any_of(policy.distributional_bucket_capacities,
                           [](const auto capacity) { return capacity < 32U; })) ||
      policy.missing_infoset_fallback != HuPreflopSampledPolicyFallback::Uniform ||
      (policy.minor < 11U && policy.current_policy_present) ||
      (policy.minor == 11U && !policy.current_policy_present) ||
      (policy.representation == HuPreflopPostflopRepresentation::ExactPhysical
           ? policy.equity_samples_per_bucket != 0U
           : policy.equity_samples_per_bucket == 0U) ||
      policy.fingerprint != fingerprint_hu_preflop_sampled_postflop_policy(policy)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  double total = 0.0;
  for (std::size_t index = 0U; index < policy.entries.size(); ++index) {
    const auto &entry = policy.entries[index];
    if (!valid_sampled_policy_key(entry.key, policy.representation) || entry.action_count == 0U ||
        entry.action_count > entry.probabilities.size() ||
        (index > 0U && !sampled_policy_key_less(policy.entries[index - 1U].key, entry.key))) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    if (is_distributional_strength_representation(policy.representation)) {
      for (std::size_t street_index = 0U; street_index < entry.key.bucket_history.size();
           ++street_index) {
        if (entry.key.bucket_history[street_index] != unset_bucket &&
            entry.key.bucket_history[street_index] >=
                policy.distributional_bucket_capacities[street_index]) {
          return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
        }
      }
    }
    const auto validate_probabilities = [&entry, &total](const auto &probabilities,
                                                         const bool required) {
      total = 0.0;
      for (std::size_t action = 0U; action < probabilities.size(); ++action) {
        const auto probability = probabilities[action];
        if (!std::isfinite(probability) || probability < 0.0 ||
            (action >= entry.action_count && probability != 0.0)) {
          return false;
        }
        if (action < entry.action_count) {
          total += probability;
        }
      }
      return required ? std::isfinite(total) && std::abs(total - 1.0) <= 1.0e-9 : total == 0.0;
    };
    if (!validate_probabilities(entry.probabilities, true) ||
        !validate_probabilities(entry.current_probabilities, policy.current_policy_present)) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
    }
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<std::array<double, hu_preflop_sampled_postflop_maximum_actions>, HuPreflopError>
query_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy,
                                         const HuPreflopSampledPostflopPolicyKey &key,
                                         const std::uint8_t action_count) {
  return query_hu_preflop_sampled_postflop_policy(policy, key, action_count,
                                                  HuPreflopSampledPolicyView::Average);
}

Result<std::array<double, hu_preflop_sampled_postflop_maximum_actions>, HuPreflopError>
query_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy,
                                         const HuPreflopSampledPostflopPolicyKey &key,
                                         const std::uint8_t action_count,
                                         const HuPreflopSampledPolicyView view) {
  using Strategy = std::array<double, hu_preflop_sampled_postflop_maximum_actions>;
  if (!supported_sampled_policy_version(policy) || policy.fingerprint.empty() ||
      static_cast<std::uint8_t>(policy.representation) >
          static_cast<std::uint8_t>(HuPreflopPostflopRepresentation::
                                        DistributionalStrengthStreetAdaptivePerfectRecallV23) ||
      action_count == 0U || action_count > hu_preflop_sampled_postflop_maximum_actions ||
      static_cast<std::uint8_t>(view) >
          static_cast<std::uint8_t>(HuPreflopSampledPolicyView::Current) ||
      !valid_sampled_policy_key(key, policy.representation)) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  if (view == HuPreflopSampledPolicyView::Current && !policy.current_policy_present) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::UnsupportedVersion);
  }
  const auto found = std::lower_bound(policy.entries.begin(), policy.entries.end(), key,
                                      [](const auto &entry, const auto &requested) {
                                        return sampled_policy_key_less(entry.key, requested);
                                      });
  if (found != policy.entries.end() && found->key == key) {
    return found->action_count == action_count
               ? Result<Strategy, HuPreflopError>::success(
                     view == HuPreflopSampledPolicyView::Current ? found->current_probabilities
                                                                 : found->probabilities)
               : Result<Strategy, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  if (policy.missing_infoset_fallback != HuPreflopSampledPolicyFallback::Uniform) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::UnsupportedVersion);
  }
  Strategy fallback{};
  std::fill_n(fallback.begin(), action_count, 1.0 / static_cast<double>(action_count));
  return Result<Strategy, HuPreflopError>::success(fallback);
}

Result<double, HuPreflopError> query_hu_preflop_sampled_postflop_action_probability(
    const HuPreflopTree &tree, const HuPreflopSampledPostflopPolicy &policy,
    const std::uint32_t entry_node, const std::array<CardId, 5> &board, const PublicState &state,
    const std::span<const Action> action_prefix, const Action &action, const ComboId combo) {
  return query_hu_preflop_sampled_postflop_action_probability(
      tree, policy, entry_node, board, state, action_prefix, action, combo,
      HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback);
}

Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>
derive_hu_preflop_sampled_postflop_public_decision(const HuPreflopTree &tree,
                                                   const std::uint32_t entry_node,
                                                   const std::array<CardId, 5> &board,
                                                   const PublicState &state,
                                                   const std::span<const Action> action_prefix) {
  if (entry_node >= tree.nodes.size() ||
      tree.nodes[entry_node].kind != HuPreflopNodeKind::PostflopEntry ||
      action_prefix.size() > 256U) {
    return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  auto replayed = tree.nodes[entry_node].state;
  auto history = mix_history(postflop_history_seed, entry_node);
  const auto action_config = sampled_postflop_action_config(tree);
  const auto advance_with_board = [&]() -> bool {
    const auto advanced = advance_street(replayed);
    if (!advanced) {
      return false;
    }
    replayed = advanced.value();
    if (replayed.street == Street::Flop) {
      const auto mask = board[0].mask() | board[1].mask() | board[2].mask();
      if (std::popcount(mask) != 3) {
        return false;
      }
      replayed.board_mask |= mask;
      return true;
    }
    const auto board_card = replayed.street == Street::Turn ? board[3] : board[4];
    if ((replayed.board_mask & board_card.mask()) != 0U) {
      return false;
    }
    replayed.board_mask |= board_card.mask();
    history = mix_history(history,
                          0x4348'414E'4345'0000ULL + static_cast<std::uint64_t>(replayed.street));
    return true;
  };
  if (!advance_with_board()) {
    return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  for (const auto &prefix_action : action_prefix) {
    while (replayed.status == HandStatus::StreetComplete) {
      if (!advance_with_board()) {
        return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
    }
    const auto legal = legal_actions(replayed, action_config);
    if (!legal || std::ranges::find(legal.value(), prefix_action) == legal.value().end()) {
      return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto next = apply_action(replayed, prefix_action, action_config);
    if (!next) {
      return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
          HuPreflopError::GameFailure);
    }
    replayed = next.value();
    history = mix_history(history, static_cast<std::uint64_t>(prefix_action.amount.units()) ^
                                       (static_cast<std::uint64_t>(prefix_action.type) << 56U));
  }
  while (replayed.status == HandStatus::StreetComplete) {
    if (!advance_with_board()) {
      return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
  }
  if (replayed != state || replayed.status != HandStatus::InProgress) {
    return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto legal = legal_actions(replayed, action_config);
  if (!legal || legal.value().empty() ||
      legal.value().size() > hu_preflop_sampled_postflop_maximum_actions) {
    return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::failure(
        legal ? HuPreflopError::InvalidConfiguration : HuPreflopError::GameFailure);
  }
  return Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>::success(
      {history, replayed.board_mask, replayed.player_to_act, replayed.street,
       static_cast<std::uint8_t>(legal.value().size())});
}

Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>
derive_hu_preflop_sampled_postflop_policy_key(
    const HuPreflopSampledPostflopPolicy &policy,
    const HuPreflopSampledPostflopPublicDecision &decision, const std::array<CardId, 5> &board,
    const ComboId combo) {
  static const auto combos = all_combos();
  if (!supported_sampled_policy_version(policy) || combo >= combos.size() || decision.player > 1U ||
      decision.street < Street::Flop || decision.street > Street::River ||
      decision.action_count == 0U ||
      decision.action_count > hu_preflop_sampled_postflop_maximum_actions) {
    return Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto hole = combos[combo];
  if (((hole.first.mask() | hole.second.mask()) & decision.public_board_mask) != 0U) {
    return Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  Deal deal;
  deal.holes[decision.player] = {hole.first, hole.second};
  deal.board = board;
  HuPreflopSampledPostflopPolicyKey key;
  key.public_history = decision.public_history;
  key.player = decision.player;
  key.street = decision.street;
  const bool forgets_preflop_class = representation_forgets_preflop_class(policy.representation);
  key.preflop_class = forgets_preflop_class ? HandClassId{0U} : hand_class(hole);
  if (policy.representation == HuPreflopPostflopRepresentation::ExactPhysical) {
    key.physical_cards = physical_cards_key(deal, decision.player, decision.street);
  } else {
    const auto final_index =
        static_cast<std::size_t>(decision.street) - static_cast<std::size_t>(Street::Flop);
    const bool current_street_only = representation_uses_current_street_only(policy.representation);
    const auto first_index = uses_one_street_history(policy.representation) && final_index > 0U
                                 ? final_index - 1U
                             : current_street_only ? final_index
                                                   : 0U;
    for (std::size_t index = first_index; index <= final_index; ++index) {
      const auto bucket_street =
          static_cast<Street>(static_cast<std::size_t>(Street::Flop) + index);
      const auto bucket =
          is_distributional_strength_representation(policy.representation)
              ? compute_distributional_strength_bucket(
                    deal, decision.player, bucket_street, policy.equity_samples_per_bucket,
                    policy.partition_seed, policy.distributional_bucket_capacities,
                    uses_distributional_profile_v6(policy.representation),
                    uses_distributional_structured_v7(policy.representation),
                    uses_distributional_street_adaptive_v8(policy.representation))
              : compute_bucket(deal, decision.player, bucket_street,
                               policy.equity_samples_per_bucket, policy.partition_seed);
      if (!bucket) {
        return Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>::failure(bucket.error());
      }
      const auto information_bucket = distributional_information_bucket(
          deal, decision.player, bucket_street, index, final_index, bucket.value(),
          policy.distributional_bucket_capacities[index], policy.representation);
      if (!information_bucket) {
        return Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>::failure(
            information_bucket.error());
      }
      key.bucket_history[index] = information_bucket.value();
    }
  }
  return valid_sampled_policy_key(key, policy.representation)
             ? Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>::success(std::move(key))
             : Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<std::array<double, hu_preflop_sampled_postflop_maximum_actions>, HuPreflopError>
query_hu_preflop_sampled_postflop_strategy(
    const HuPreflopTree &tree, const HuPreflopSampledPostflopPolicy &policy,
    const std::uint32_t entry_node, const std::array<CardId, 5> &board, const PublicState &state,
    const std::span<const Action> action_prefix, const ComboId combo,
    const HuPreflopSampledPolicyLookupMode lookup_mode) {
  using Strategy = std::array<double, hu_preflop_sampled_postflop_maximum_actions>;
  static const auto combos = all_combos();
  if (tree.fingerprint != policy.tree_fingerprint || entry_node >= tree.nodes.size() ||
      tree.nodes[entry_node].kind != HuPreflopNodeKind::PostflopEntry || combo >= combos.size() ||
      action_prefix.size() > 256U ||
      static_cast<std::uint8_t>(lookup_mode) >
          static_cast<std::uint8_t>(HuPreflopSampledPolicyLookupMode::RequireTrainedInfoset)) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto hole = combos[combo];
  auto replayed = tree.nodes[entry_node].state;
  auto history = mix_history(postflop_history_seed, entry_node);
  const auto action_config = sampled_postflop_action_config(tree);

  const auto advance_with_board = [&]() -> bool {
    const auto advanced = advance_street(replayed);
    if (!advanced) {
      return false;
    }
    replayed = advanced.value();
    if (replayed.street == Street::Flop) {
      const auto mask = board[0].mask() | board[1].mask() | board[2].mask();
      if (std::popcount(mask) != 3) {
        return false;
      }
      replayed.board_mask |= mask;
      return true;
    }
    const auto board_card = replayed.street == Street::Turn ? board[3] : board[4];
    if ((replayed.board_mask & board_card.mask()) != 0U) {
      return false;
    }
    replayed.board_mask |= board_card.mask();
    history = mix_history(history,
                          0x4348'414E'4345'0000ULL + static_cast<std::uint64_t>(replayed.street));
    return true;
  };

  if (!advance_with_board()) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  for (const auto &prefix_action : action_prefix) {
    while (replayed.status == HandStatus::StreetComplete) {
      if (!advance_with_board()) {
        return Result<Strategy, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
    }
    const auto legal = legal_actions(replayed, action_config);
    if (!legal || std::ranges::find(legal.value(), prefix_action) == legal.value().end()) {
      return Result<Strategy, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    const auto next = apply_action(replayed, prefix_action, action_config);
    if (!next) {
      return Result<Strategy, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    replayed = next.value();
    history = mix_history(history, static_cast<std::uint64_t>(prefix_action.amount.units()) ^
                                       (static_cast<std::uint64_t>(prefix_action.type) << 56U));
  }
  while (replayed.status == HandStatus::StreetComplete) {
    if (!advance_with_board()) {
      return Result<Strategy, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
  }
  if (replayed != state || replayed.status != HandStatus::InProgress ||
      ((hole.first.mask() | hole.second.mask()) & replayed.board_mask) != 0U) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto legal = legal_actions(replayed, action_config);
  if (!legal) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  if (legal.value().empty() || legal.value().size() > hu_preflop_sampled_postflop_maximum_actions) {
    return Result<Strategy, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }

  Deal deal;
  deal.holes[replayed.player_to_act] = {hole.first, hole.second};
  deal.board = board;
  HuPreflopSampledPostflopPolicyKey key;
  key.public_history = history;
  key.player = replayed.player_to_act;
  key.street = replayed.street;
  const bool forgets_preflop_class = representation_forgets_preflop_class(policy.representation);
  key.preflop_class = forgets_preflop_class ? HandClassId{0U} : hand_class(hole);
  if (policy.representation == HuPreflopPostflopRepresentation::ExactPhysical) {
    key.physical_cards = physical_cards_key(deal, replayed.player_to_act, replayed.street);
  } else {
    const auto final_index =
        static_cast<std::size_t>(replayed.street) - static_cast<std::size_t>(Street::Flop);
    const bool current_street_only = representation_uses_current_street_only(policy.representation);
    const auto first_index = uses_one_street_history(policy.representation) && final_index > 0U
                                 ? final_index - 1U
                             : current_street_only ? final_index
                                                   : 0U;
    for (std::size_t index = first_index; index <= final_index; ++index) {
      const auto bucket_street =
          static_cast<Street>(static_cast<std::size_t>(Street::Flop) + index);
      const auto bucket =
          is_distributional_strength_representation(policy.representation)
              ? compute_distributional_strength_bucket(
                    deal, replayed.player_to_act, bucket_street, policy.equity_samples_per_bucket,
                    policy.partition_seed, policy.distributional_bucket_capacities,
                    uses_distributional_profile_v6(policy.representation),
                    uses_distributional_structured_v7(policy.representation),
                    uses_distributional_street_adaptive_v8(policy.representation))
              : compute_bucket(deal, replayed.player_to_act, bucket_street,
                               policy.equity_samples_per_bucket, policy.partition_seed);
      if (!bucket) {
        return Result<Strategy, HuPreflopError>::failure(bucket.error());
      }
      const auto information_bucket = distributional_information_bucket(
          deal, replayed.player_to_act, bucket_street, index, final_index, bucket.value(),
          policy.distributional_bucket_capacities[index], policy.representation);
      if (!information_bucket) {
        return Result<Strategy, HuPreflopError>::failure(information_bucket.error());
      }
      key.bucket_history[index] = information_bucket.value();
    }
  }
  if (lookup_mode == HuPreflopSampledPolicyLookupMode::RequireTrainedInfoset) {
    const auto found = std::lower_bound(policy.entries.begin(), policy.entries.end(), key,
                                        [](const auto &entry, const auto &requested) {
                                          return sampled_policy_key_less(entry.key, requested);
                                        });
    if (found == policy.entries.end() || found->key != key) {
      return Result<Strategy, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
  }
  return query_hu_preflop_sampled_postflop_policy(policy, key,
                                                  static_cast<std::uint8_t>(legal.value().size()));
}

Result<double, HuPreflopError> query_hu_preflop_sampled_postflop_action_probability(
    const HuPreflopTree &tree, const HuPreflopSampledPostflopPolicy &policy,
    const std::uint32_t entry_node, const std::array<CardId, 5> &board, const PublicState &state,
    const std::span<const Action> action_prefix, const Action &action, const ComboId combo,
    const HuPreflopSampledPolicyLookupMode lookup_mode) {
  const auto strategy = query_hu_preflop_sampled_postflop_strategy(
      tree, policy, entry_node, board, state, action_prefix, combo, lookup_mode);
  if (!strategy) {
    return Result<double, HuPreflopError>::failure(strategy.error());
  }
  const auto legal = legal_actions(state, sampled_postflop_action_config(tree));
  if (!legal) {
    return Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  const auto selected = std::ranges::find(legal.value(), action);
  return selected != legal.value().end() &&
                 legal.value().size() <= hu_preflop_sampled_postflop_maximum_actions
             ? Result<double, HuPreflopError>::success(
                   strategy.value()[static_cast<std::size_t>(selected - legal.value().begin())])
             : Result<double, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
}

Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_category_equity_bucket(const std::array<CardId, 2> &hole,
                                          const std::array<CardId, 5> &board, const Street street,
                                          const std::uint32_t samples, const std::uint64_t seed) {
  Deal deal;
  deal.holes[0] = hole;
  deal.board = board;
  return compute_bucket(deal, 0U, street, samples, seed);
}

Result<std::uint16_t, HuPreflopError> compute_hu_preflop_distributional_strength_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, const Street street,
    const std::uint32_t samples, const std::uint64_t partition_seed) {
  Deal deal;
  deal.holes[0] = hole;
  deal.board = board;
  return compute_distributional_strength_bucket(deal, 0U, street, samples, partition_seed,
                                                hu_preflop_distributional_default_capacities, false,
                                                false, false);
}

Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>
sample_hu_preflop_public_board_private_deals(const std::array<CardId, 5> &board,
                                             const std::uint8_t traverser,
                                             const std::uint32_t sample_count,
                                             const std::uint64_t seed) {
  if (seed == 0U) {
    return Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  std::mt19937_64 random(seed);
  return sample_public_board_private_deals(board, traverser, sample_count, random);
}

Result<std::vector<HuPreflopRootConditionalDeal>, HuPreflopError>
sample_hu_preflop_root_conditional_deals(const HandClassId hand_class_id,
                                         const std::uint8_t traverser,
                                         const std::uint32_t sample_count,
                                         const std::uint64_t seed) {
  if (hand_class_id >= hu_preflop_hand_class_count || traverser > 1U || sample_count == 0U ||
      seed == 0U) {
    return Result<std::vector<HuPreflopRootConditionalDeal>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  try {
    std::mt19937_64 random(seed);
    std::vector<HuPreflopRootConditionalDeal> output;
    output.reserve(sample_count);
    for (std::uint32_t sample = 0U; sample < sample_count; ++sample) {
      const auto deal = sample_deal_conditioned_on_class(hand_class_id, traverser, random);
      if (!deal) {
        return Result<std::vector<HuPreflopRootConditionalDeal>, HuPreflopError>::failure(
            deal.error());
      }
      output.push_back(HuPreflopRootConditionalDeal{deal.value().holes, deal.value().board});
    }
    return Result<std::vector<HuPreflopRootConditionalDeal>, HuPreflopError>::success(
        std::move(output));
  } catch (const std::bad_alloc &) {
    return Result<std::vector<HuPreflopRootConditionalDeal>, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

Result<std::uint16_t, HuPreflopError> compute_hu_preflop_distributional_strength_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, const Street street,
    const std::uint32_t samples, const std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities) {
  Deal deal;
  deal.holes[0] = hole;
  deal.board = board;
  return compute_distributional_strength_bucket(deal, 0U, street, samples, partition_seed,
                                                capacities, false, false, false);
}

Result<std::uint16_t, HuPreflopError> compute_hu_preflop_distributional_profile_v6_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, const Street street,
    const std::uint32_t samples, const std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities) {
  Deal deal;
  deal.holes[0] = hole;
  deal.board = board;
  return compute_distributional_strength_bucket(deal, 0U, street, samples, partition_seed,
                                                capacities, true, false, false);
}

Result<std::uint16_t, HuPreflopError> compute_hu_preflop_distributional_structured_v7_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, const Street street,
    const std::uint32_t samples, const std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities) {
  Deal deal;
  deal.holes[0] = hole;
  deal.board = board;
  return compute_distributional_strength_bucket(deal, 0U, street, samples, partition_seed,
                                                capacities, false, true, false);
}

Result<std::uint16_t, HuPreflopError> compute_hu_preflop_distributional_street_adaptive_v8_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, const Street street,
    const std::uint32_t samples, const std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities) {
  Deal deal;
  deal.holes[0] = hole;
  deal.board = board;
  return compute_distributional_strength_bucket(deal, 0U, street, samples, partition_seed,
                                                capacities, false, false, true);
}

Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_distributional_adaptive_category_history_v11_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, const Street street,
    const std::uint32_t samples, const std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities) {
  return compute_hu_preflop_distributional_street_adaptive_v8_bucket(hole, board, street, samples,
                                                                     partition_seed, capacities);
}

Result<HuPreflopRecallAudit, HuPreflopError>
audit_hu_preflop_postflop_recall_contract(const HuPreflopPostflopRepresentation representation) {
  if (static_cast<std::uint8_t>(representation) >
      static_cast<std::uint8_t>(
          HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23)) {
    return Result<HuPreflopRecallAudit, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }

  HuPreflopRecallAudit audit;
  audit.representation = representation;
  audit.retains_complete_public_history = true;
  audit.retains_preflop_class = !representation_forgets_preflop_class(representation);

  const bool retains_all_private_observations =
      representation == HuPreflopPostflopRepresentation::ExactPhysical ||
      representation == HuPreflopPostflopRepresentation::CategoryEquityMonteCarlo ||
      representation == HuPreflopPostflopRepresentation::DistributionalStrengthPerfectRecall ||
      representation == HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory ||
      representation ==
          HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23;
  const bool retains_previous_exact_bucket =
      representation == HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9;

  for (std::size_t decision = 0U; decision < audit.retains_bucket_observation.size(); ++decision) {
    for (std::size_t observation = 0U; observation <= decision; ++observation) {
      audit.retains_bucket_observation[decision][observation] =
          observation == decision || retains_all_private_observations ||
          (retains_previous_exact_bucket && observation + 1U == decision);
    }
  }

  const auto add_witness = [&audit](const HuPreflopRecallObservationKind kind,
                                    const Street decision_street,
                                    const Street forgotten_observation_street) {
    if (audit.witness_count >= audit.witnesses.size()) {
      return;
    }
    audit.witnesses[audit.witness_count++] =
        HuPreflopRecallWitness{kind, decision_street, forgotten_observation_street};
  };

  for (std::size_t decision = 0U; decision < audit.retains_bucket_observation.size(); ++decision) {
    const auto decision_street =
        static_cast<Street>(static_cast<std::size_t>(Street::Flop) + decision);
    if (!audit.retains_preflop_class) {
      add_witness(HuPreflopRecallObservationKind::PreflopClass, decision_street, Street::Preflop);
    }
    for (std::size_t observation = 0U; observation < decision; ++observation) {
      if (!audit.retains_bucket_observation[decision][observation]) {
        add_witness(HuPreflopRecallObservationKind::PostflopBucket, decision_street,
                    static_cast<Street>(static_cast<std::size_t>(Street::Flop) + observation));
      }
    }
  }

  audit.perfect_recall = audit.retains_complete_public_history && audit.retains_preflop_class;
  for (std::size_t decision = 0U;
       decision < audit.retains_bucket_observation.size() && audit.perfect_recall; ++decision) {
    for (std::size_t observation = 0U; observation < decision; ++observation) {
      audit.perfect_recall =
          audit.perfect_recall && audit.retains_bucket_observation[decision][observation];
    }
  }
  return Result<HuPreflopRecallAudit, HuPreflopError>::success(audit);
}

std::string
fingerprint_hu_preflop_abstract_game_definition(const HuPreflopAbstractGameDefinition &definition) {
  auto hash = policy_fingerprint_offset;
  mix_policy_fingerprint_string(hash, "gtosd.hu_preflop_abstract_game_definition.v1");
  mix_policy_fingerprint_unsigned(hash, definition.major);
  mix_policy_fingerprint_unsigned(hash, definition.minor);
  mix_policy_fingerprint_string(hash, definition.rules_fingerprint);
  mix_policy_fingerprint_string(hash, definition.tree_fingerprint);
  mix_policy_fingerprint_string(hash, definition.abstraction_fingerprint);
  mix_policy_fingerprint_string(hash, definition.abstraction_id);
  mix_policy_fingerprint_string(hash, definition.chance_model_id);
  mix_policy_fingerprint_unsigned(hash, definition.partition_seed);
  mix_policy_fingerprint_unsigned(hash, definition.equity_samples_per_bucket);
  for (const auto capacity : definition.distributional_bucket_capacities) {
    mix_policy_fingerprint_unsigned(hash, capacity);
  }
  mix_policy_fingerprint_unsigned(hash, static_cast<std::uint8_t>(definition.representation));
  mix_policy_fingerprint_unsigned(
      hash, static_cast<std::uint8_t>(definition.recall_contract.perfect_recall));
  mix_policy_fingerprint_unsigned(hash, definition.preflop_tree.node_count);
  mix_policy_fingerprint_unsigned(hash, definition.preflop_tree.edge_count);
  mix_policy_fingerprint_unsigned(hash, definition.preflop_tree.decision_nodes);
  mix_policy_fingerprint_unsigned(hash, definition.preflop_tree.postflop_entries);
  mix_policy_fingerprint_unsigned(hash, definition.preflop_tree.terminal_folds);
  mix_policy_fingerprint_unsigned(hash, definition.preflop_tree.terminal_all_ins);
  mix_policy_fingerprint_unsigned(hash, definition.preflop_tree.maximum_depth);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.represented_nodes);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.action_edges);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.decision_nodes);
  for (const auto decisions : definition.postflop_public_tree.decision_nodes_by_street) {
    mix_policy_fingerprint_unsigned(hash, decisions);
  }
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.chance_frontiers);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.terminal_folds);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.terminal_showdowns);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.terminal_all_in_runouts);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.memoized_states);
  mix_policy_fingerprint_unsigned(hash, definition.postflop_public_tree.maximum_subtree_depth);
  mix_policy_fingerprint_unsigned(hash,
                                  definition.postflop_public_tree.maximum_observed_raise_count);
  mix_policy_fingerprint_unsigned(
      hash,
      static_cast<std::uint8_t>(definition.postflop_public_tree.core_raise_safety_limit_reached));
  mix_policy_fingerprint_unsigned(
      hash,
      static_cast<std::uint8_t>(definition.postflop_public_tree.natural_stack_termination_proven));
  mix_policy_fingerprint_unsigned(hash, definition.preflop_information_set_cartesian_upper_bound);
  for (const auto value : definition.private_observation_cartesian_upper_bound_by_street) {
    mix_policy_fingerprint_unsigned(hash, value);
  }
  for (const auto value : definition.information_set_cartesian_upper_bound_by_street) {
    mix_policy_fingerprint_unsigned(hash, value);
  }
  mix_policy_fingerprint_unsigned(hash, definition.total_information_set_cartesian_upper_bound);
  mix_policy_fingerprint_unsigned(
      hash, static_cast<std::uint8_t>(definition.cartesian_upper_bound_overflow));
  mix_policy_fingerprint_unsigned(
      hash, static_cast<std::uint8_t>(definition.reachable_information_set_census_complete));
  mix_policy_fingerprint_unsigned(
      hash, static_cast<std::uint8_t>(definition.exact_chance_model_compiled));
  mix_policy_fingerprint_unsigned(
      hash, static_cast<std::uint8_t>(definition.exact_abstract_nashconv_certifiable));
  return finish_policy_fingerprint(hash);
}

Result<bool, HuPreflopError>
validate_hu_preflop_abstract_game_definition(const HuPreflopTree &tree,
                                             const HuPreflopAbstractGameDefinition &definition) {
  const auto public_tree = analyze_hu_postflop_public_skeleton(tree);
  if (!public_tree) {
    return Result<bool, HuPreflopError>::failure(public_tree.error());
  }
  const auto decision_total = std::accumulate(
      definition.postflop_public_tree.decision_nodes_by_street.begin(),
      definition.postflop_public_tree.decision_nodes_by_street.end(), std::uint64_t{0U});
  if (definition.major != HuPreflopAbstractGameDefinition::format_major ||
      definition.minor != HuPreflopAbstractGameDefinition::format_minor ||
      definition.representation !=
          HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23 ||
      definition.rules_fingerprint != abstract_game_rules_fingerprint(tree.config) ||
      definition.tree_fingerprint != tree.fingerprint || definition.abstraction_id.empty() ||
      definition.abstraction_fingerprint != abstract_game_abstraction_fingerprint(definition) ||
      definition.chance_model_id != "online_physical_deal_sampling_not_compiled_v1" ||
      definition.partition_seed == 0U || definition.equity_samples_per_bucket == 0U ||
      std::ranges::any_of(definition.distributional_bucket_capacities,
                          [](const auto capacity) {
                            return capacity < 32U || capacity > 32'768U ||
                                   !std::has_single_bit(capacity);
                          }) ||
      !definition.recall_contract.perfect_recall ||
      definition.recall_contract.representation != definition.representation ||
      !same_preflop_tree_stats(definition.preflop_tree, tree.stats) ||
      !same_postflop_public_stats(definition.postflop_public_tree, public_tree.value()) ||
      decision_total != definition.postflop_public_tree.decision_nodes ||
      definition.cartesian_upper_bound_overflow ||
      definition.reachable_information_set_census_complete ||
      definition.exact_chance_model_compiled || definition.exact_abstract_nashconv_certifiable ||
      definition.fingerprint.empty() ||
      definition.fingerprint != fingerprint_hu_preflop_abstract_game_definition(definition)) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }

  std::uint64_t expected_private_states = hu_preflop_hand_class_count;
  std::uint64_t expected_total = 0U;
  std::uint64_t expected_preflop = 0U;
  if (!checked_multiply_or_saturate(tree.stats.decision_nodes, hu_preflop_hand_class_count,
                                    expected_preflop) ||
      expected_preflop != definition.preflop_information_set_cartesian_upper_bound) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  expected_total = expected_preflop;
  for (std::size_t street = 0U; street < definition.distributional_bucket_capacities.size();
       ++street) {
    std::uint64_t next_private_states = 0U;
    std::uint64_t street_information_sets = 0U;
    std::uint64_t next_total = 0U;
    if (!checked_multiply_or_saturate(expected_private_states,
                                      definition.distributional_bucket_capacities[street],
                                      next_private_states) ||
        !checked_multiply_or_saturate(
            next_private_states, definition.postflop_public_tree.decision_nodes_by_street[street],
            street_information_sets) ||
        !checked_add_or_saturate(expected_total, street_information_sets, next_total) ||
        definition.private_observation_cartesian_upper_bound_by_street[street] !=
            next_private_states ||
        definition.information_set_cartesian_upper_bound_by_street[street] !=
            street_information_sets) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    expected_private_states = next_private_states;
    expected_total = next_total;
  }
  if (expected_total != definition.total_information_set_cartesian_upper_bound) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<HuPreflopAbstractGameDefinition, HuPreflopError>
make_hu_preflop_abstract_game_definition(const HuPreflopTree &tree,
                                         const HuPreflopSolveOptions &options) {
  if (!validate_hu_preflop_config(tree.config) || tree.nodes.empty() ||
      tree.root >= tree.nodes.size() || tree.fingerprint.empty() ||
      options.postflop_representation !=
          HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23 ||
      options.partition_seed == 0U || options.equity_samples_per_bucket == 0U ||
      std::ranges::any_of(options.distributional_bucket_capacities, [](const auto capacity) {
        return capacity < 32U || capacity > 32'768U || !std::has_single_bit(capacity);
      })) {
    return Result<HuPreflopAbstractGameDefinition, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto recall = audit_hu_preflop_postflop_recall_contract(options.postflop_representation);
  const auto public_tree = analyze_hu_postflop_public_skeleton(tree);
  if (!recall || !recall.value().perfect_recall || !public_tree) {
    return Result<HuPreflopAbstractGameDefinition, HuPreflopError>::failure(
        !recall        ? recall.error()
        : !public_tree ? public_tree.error()
                       : HuPreflopError::IntegrityFailure);
  }

  HuPreflopAbstractGameDefinition result;
  result.rules_fingerprint = abstract_game_rules_fingerprint(tree.config);
  result.tree_fingerprint = tree.fingerprint;
  result.abstraction_id = postflop_abstraction_id(options);
  result.partition_seed = options.partition_seed;
  result.equity_samples_per_bucket = options.equity_samples_per_bucket;
  result.distributional_bucket_capacities = options.distributional_bucket_capacities;
  result.representation = options.postflop_representation;
  result.recall_contract = recall.value();
  result.preflop_tree = tree.stats;
  result.postflop_public_tree = public_tree.value();
  result.abstraction_fingerprint = abstract_game_abstraction_fingerprint(result);

  bool exact = checked_multiply_or_saturate(tree.stats.decision_nodes, hu_preflop_hand_class_count,
                                            result.preflop_information_set_cartesian_upper_bound);
  result.total_information_set_cartesian_upper_bound =
      result.preflop_information_set_cartesian_upper_bound;
  std::uint64_t private_states = hu_preflop_hand_class_count;
  for (std::size_t street = 0U; street < result.distributional_bucket_capacities.size(); ++street) {
    exact = checked_multiply_or_saturate(
                private_states, result.distributional_bucket_capacities[street],
                result.private_observation_cartesian_upper_bound_by_street[street]) &&
            exact;
    private_states = result.private_observation_cartesian_upper_bound_by_street[street];
    exact = checked_multiply_or_saturate(
                private_states, result.postflop_public_tree.decision_nodes_by_street[street],
                result.information_set_cartesian_upper_bound_by_street[street]) &&
            exact;
    std::uint64_t next_total = 0U;
    exact = checked_add_or_saturate(result.total_information_set_cartesian_upper_bound,
                                    result.information_set_cartesian_upper_bound_by_street[street],
                                    next_total) &&
            exact;
    result.total_information_set_cartesian_upper_bound = next_total;
  }
  result.cartesian_upper_bound_overflow = !exact;
  result.fingerprint = fingerprint_hu_preflop_abstract_game_definition(result);
  const auto valid = validate_hu_preflop_abstract_game_definition(tree, result);
  return valid ? Result<HuPreflopAbstractGameDefinition, HuPreflopError>::success(std::move(result))
               : Result<HuPreflopAbstractGameDefinition, HuPreflopError>::failure(valid.error());
}

Result<HuPreflopSolveResult, HuPreflopError>
solve_hu_preflop_sampled(const HuPreflopTree &tree, const HuPreflopSolveOptions &options) {
  const auto parallel_jobs = static_cast<std::uint64_t>(options.training_batch_iterations) * 2U;
  const auto shadow_workers = options.root_action_value_rollouts > 1U
                                  ? static_cast<std::uint64_t>(options.worker_threads)
                                  : 0U;
  const bool parallel_group_overflow =
      parallel_jobs > std::numeric_limits<std::uint64_t>::max() - shadow_workers;
  const auto parallel_update_groups = parallel_group_overflow ? 0U : parallel_jobs + shadow_workers;
  const bool parallel_job_payload_overflow =
      parallel_jobs > options.maximum_parallel_scratch_bytes / sizeof(PendingTraversal);
  const auto parallel_update_budget =
      parallel_job_payload_overflow
          ? 0U
          : options.maximum_parallel_scratch_bytes - parallel_jobs * sizeof(PendingTraversal);
  const bool parallel_scratch_overflow =
      parallel_jobs != 0U &&
      (parallel_job_payload_overflow || parallel_group_overflow ||
       options.maximum_parallel_updates_per_job >
           std::numeric_limits<std::uint64_t>::max() / parallel_update_groups ||
       parallel_update_groups * options.maximum_parallel_updates_per_job >
           parallel_update_budget / (2U * sizeof(PendingInformationUpdate)));
  std::bitset<hu_preflop_hand_class_count> traced_hand_classes;
  bool invalid_root_decision_trace =
      options.root_decision_trace_hand_classes.size() > maximum_root_decision_trace_hand_classes ||
      (!options.root_decision_trace_hand_classes.empty() &&
       (options.root_decision_trace_deals_per_class == 0U ||
        options.root_decision_trace_deals_per_class > maximum_root_decision_trace_deals_per_class ||
        options.root_decision_trace_hand_classes.size() *
                static_cast<std::uint64_t>(options.root_decision_trace_deals_per_class) >
            maximum_root_decision_trace_class_deals));
  for (const auto hand_class_id : options.root_decision_trace_hand_classes) {
    if (hand_class_id >= hu_preflop_hand_class_count || traced_hand_classes.test(hand_class_id)) {
      invalid_root_decision_trace = true;
      break;
    }
    traced_hand_classes.set(hand_class_id);
  }
  if (!validate_hu_preflop_config(tree.config) || tree.nodes.empty() || options.iterations == 0U ||
      options.evaluation_deals == 0U || options.best_response_iterations == 0U ||
      options.best_response_evaluation_deals == 0U || options.partition_seed == 0U ||
      options.evaluation_seed == 0U || options.maximum_bucket_cache_entries < 6U ||
      (options.collect_action_conditioned_telemetry &&
       options.maximum_action_conditioned_telemetry_entries == 0U) ||
      invalid_root_decision_trace || options.root_action_value_rollouts == 0U ||
      options.root_action_value_rollouts > 8U ||
      (options.root_continuation_mean_updates &&
       (options.root_action_value_rollouts <= 1U || options.use_opponent_value_baseline ||
        options.sampling_algorithm != HuPreflopSamplingAlgorithm::LinearMccfr)) ||
      (options.symmetric_traverser_mean_updates && !options.root_continuation_mean_updates) ||
      (options.root_common_random_numbers && options.training_batch_iterations == 0U) ||
      (options.global_common_random_numbers && options.training_batch_iterations == 0U) ||
      (options.root_first_opponent_response_stratification &&
       (options.root_action_value_rollouts <= 1U || !options.root_continuation_mean_updates ||
        !options.symmetric_traverser_mean_updates || options.training_batch_iterations == 0U ||
        options.chance_sampling_mode != HuPreflopChanceSamplingMode::IndependentPhysical)) ||
      (options.root_action_value_rollouts > 1U &&
       (options.training_batch_iterations == 0U ||
        options.chance_sampling_mode != HuPreflopChanceSamplingMode::IndependentPhysical)) ||
      options.maximum_numeric_state_bytes < sizeof(InformationKey) + sizeof(InformationState) ||
      (options.use_opponent_value_baseline &&
       (options.training_batch_iterations == 0U ||
        options.maximum_variance_baseline_bytes <
            sizeof(InformationKey) + sizeof(OpponentValueBaselineState))) ||
      options.worker_threads == 0U || options.worker_threads > 8U ||
      static_cast<std::uint8_t>(options.chance_sampling_mode) >
          static_cast<std::uint8_t>(HuPreflopChanceSamplingMode::PublicBoardStratified) ||
      static_cast<std::uint8_t>(options.postflop_all_in_expectation_mode) >
          static_cast<std::uint8_t>(HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn) ||
      (options.chance_sampling_mode == HuPreflopChanceSamplingMode::PublicBoardStratified &&
       (options.training_batch_iterations == 0U || options.training_batch_iterations > 465U)) ||
      (options.training_batch_iterations == 0U && options.worker_threads != 1U) ||
      (options.training_batch_iterations != 0U &&
       (options.sampling_algorithm == HuPreflopSamplingAlgorithm::DiscountedMccfr1503 ||
        options.sampling_algorithm == HuPreflopSamplingAlgorithm::ChanceSampledCfr ||
        (options.preflop_refinement_iterations != 0U &&
         options.sampling_algorithm != HuPreflopSamplingAlgorithm::LinearMccfr) ||
        options.maximum_parallel_updates_per_job == 0U ||
        options.maximum_parallel_scratch_bytes < sizeof(PendingTraversal) ||
        parallel_scratch_overflow ||
        options.maximum_bucket_cache_entries <
            static_cast<std::uint64_t>(options.worker_threads) * 6U)) ||
      std::ranges::any_of(options.distributional_bucket_capacities,
                          [](const auto capacity) {
                            return capacity < 2U || capacity > 32'768U ||
                                   !std::has_single_bit(capacity);
                          }) ||
      static_cast<std::uint8_t>(options.sampling_algorithm) >
          static_cast<std::uint8_t>(HuPreflopSamplingAlgorithm::ChanceSampledCfr) ||
      static_cast<std::uint8_t>(options.postflop_representation) >
          static_cast<std::uint8_t>(HuPreflopPostflopRepresentation::
                                        DistributionalStrengthStreetAdaptivePerfectRecallV23) ||
      ((options.postflop_representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 ||
        options.postflop_representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 ||
        options.postflop_representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9 ||
        options.postflop_representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10 ||
        options.postflop_representation ==
            HuPreflopPostflopRepresentation::DistributionalStrengthAdaptiveCategoryHistoryV11 ||
        options.postflop_representation ==
            HuPreflopPostflopRepresentation::
                DistributionalStrengthStreetAdaptivePerfectRecallV23) &&
       std::ranges::any_of(options.distributional_bucket_capacities,
                           [](const auto capacity) { return capacity < 32U; })) ||
      (options.postflop_representation != HuPreflopPostflopRepresentation::ExactPhysical &&
       options.equity_samples_per_bucket == 0U) ||
      options.preflop_refinement_iterations >
          std::numeric_limits<std::uint64_t>::max() - options.iterations ||
      options.iterations >= std::numeric_limits<std::size_t>::max() ||
      options.preflop_refinement_iterations >=
          std::numeric_limits<std::size_t>::max() - options.iterations ||
      options.best_response_iterations >= std::numeric_limits<std::size_t>::max()) {
    return Result<HuPreflopSolveResult, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  return SampledSolver(tree, options).solve();
}

} // namespace gtosd
