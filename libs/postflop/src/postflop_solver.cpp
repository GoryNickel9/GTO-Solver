#include "gtosd/postflop/postflop_solver.hpp"

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace gtosd {
namespace {

constexpr std::size_t combo_count = 630U;
constexpr std::size_t maximum_action_count = 8U;
constexpr std::size_t maximum_parallel_action_count = 3U;
constexpr std::size_t compact_combo_capacity = 36U;
constexpr double units_per_ante = 10'000.0;
using DenseComboVector = std::array<double, combo_count>;
template <std::size_t Capacity> using TraversalComboVector = std::array<double, Capacity>;

class PagedActionFile;

struct ActionBuffers {
  double *regret{nullptr};
  double *strategy{nullptr};
  std::size_t count{0};
  PagedActionFile *paged{nullptr};
  float *regret_float32{nullptr};
  float *strategy_float32{nullptr};

  [[nodiscard]] double regret_at(std::size_t index) const;
  [[nodiscard]] double strategy_at(std::size_t index) const;
  void set_regret(std::size_t index, double value) const;
  void add_strategy(std::size_t index, double value) const;
};

class PagedActionFile {
public:
  PagedActionFile(const std::string &path, const std::size_t action_count, const bool create)
      : path_(path), action_count_(action_count) {
    if (path.empty() || action_count == 0U ||
        action_count > std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
      return;
    }
    size_ = static_cast<std::uint64_t>(action_count) * 2U * sizeof(double);
    if (create) {
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      if (!output) {
        return;
      }
      output.seekp(static_cast<std::streamoff>(size_ - 1U));
      output.put('\0');
      output.flush();
      if (!output) {
        return;
      }
    } else {
      std::error_code size_error;
      if (std::filesystem::file_size(path, size_error) != size_ || size_error) {
        return;
      }
    }
    file_.open(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file_) {
      return;
    }
    pages_.resize(resident_page_count);
    for (auto &page : pages_) {
      page.bytes.resize(page_size_bytes);
    }
    valid_ = true;
  }

  PagedActionFile(const PagedActionFile &) = delete;
  PagedActionFile &operator=(const PagedActionFile &) = delete;

  ~PagedActionFile() { static_cast<void>(flush()); }

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] ActionBuffers buffers() noexcept { return {nullptr, nullptr, action_count_, this}; }
  [[nodiscard]] const std::string &path() const noexcept { return path_; }

  [[nodiscard]] double get(const std::size_t logical_index) {
    const auto byte_offset = static_cast<std::uint64_t>(logical_index) * sizeof(double);
    auto *const page = load_page(byte_offset / page_size_bytes);
    if (page == nullptr) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    double value = 0.0;
    std::memcpy(&value, page->bytes.data() + byte_offset % page_size_bytes, sizeof(value));
    return value;
  }

  void set(const std::size_t logical_index, const double value) {
    const auto byte_offset = static_cast<std::uint64_t>(logical_index) * sizeof(double);
    auto *const page = load_page(byte_offset / page_size_bytes);
    if (page == nullptr) {
      return;
    }
    std::memcpy(page->bytes.data() + byte_offset % page_size_bytes, &value, sizeof(value));
    page->dirty = true;
  }

  [[nodiscard]] bool flush() {
    if (!valid_) {
      return false;
    }
    for (auto &page : pages_) {
      if (!flush_page(page)) {
        valid_ = false;
        return false;
      }
    }
    file_.flush();
    valid_ = static_cast<bool>(file_);
    return valid_;
  }

private:
  static constexpr std::uint64_t page_size_bytes = 64U * 1'024U;
  static constexpr std::size_t resident_page_count = 64U;
  static constexpr std::uint64_t invalid_page = std::numeric_limits<std::uint64_t>::max();

  struct Page {
    std::uint64_t index{invalid_page};
    std::uint64_t stamp{0};
    bool dirty{false};
    std::vector<char> bytes;
  };

  bool flush_page(Page &page) {
    if (page.index == invalid_page || !page.dirty) {
      return true;
    }
    const auto offset = page.index * page_size_bytes;
    const auto bytes = static_cast<std::streamsize>(std::min(page_size_bytes, size_ - offset));
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(offset));
    file_.write(page.bytes.data(), bytes);
    if (!file_) {
      return false;
    }
    page.dirty = false;
    return true;
  }

  Page *load_page(const std::uint64_t page_index) {
    const auto found = page_lookup_.find(page_index);
    if (found != page_lookup_.end()) {
      auto &page = pages_[found->second];
      page.stamp = ++stamp_;
      return &page;
    }
    std::size_t slot = pages_.size();
    for (std::size_t index = 0; index < pages_.size(); ++index) {
      if (pages_[index].index == invalid_page) {
        slot = index;
        break;
      }
    }
    if (slot == pages_.size()) {
      slot = static_cast<std::size_t>(std::min_element(pages_.begin(), pages_.end(),
                                                       [](const Page &left, const Page &right) {
                                                         return left.stamp < right.stamp;
                                                       }) -
                                      pages_.begin());
    }
    auto &page = pages_[slot];
    if (!flush_page(page)) {
      valid_ = false;
      return nullptr;
    }
    if (page.index != invalid_page) {
      page_lookup_.erase(page.index);
    }
    std::fill(page.bytes.begin(), page.bytes.end(), '\0');
    const auto offset = page_index * page_size_bytes;
    const auto bytes = static_cast<std::streamsize>(std::min(page_size_bytes, size_ - offset));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(page.bytes.data(), bytes);
    if (file_.gcount() != bytes) {
      valid_ = false;
      return nullptr;
    }
    page.index = page_index;
    page.stamp = ++stamp_;
    page.dirty = false;
    page_lookup_[page_index] = slot;
    return &page;
  }

  std::string path_;
  std::size_t action_count_{0};
  std::uint64_t size_{0};
  std::fstream file_;
  std::vector<Page> pages_;
  std::unordered_map<std::uint64_t, std::size_t> page_lookup_;
  std::uint64_t stamp_{0};
  bool valid_{false};
};

double ActionBuffers::regret_at(const std::size_t index) const {
  if (paged != nullptr) {
    return paged->get(index);
  }
  return regret_float32 != nullptr ? static_cast<double>(regret_float32[index]) : regret[index];
}

double ActionBuffers::strategy_at(const std::size_t index) const {
  if (paged != nullptr) {
    return paged->get(count + index);
  }
  return strategy_float32 != nullptr ? static_cast<double>(strategy_float32[index])
                                     : strategy[index];
}

void ActionBuffers::set_regret(const std::size_t index, const double value) const {
  if (paged != nullptr) {
    paged->set(index, value);
  } else if (regret_float32 != nullptr) {
    regret_float32[index] = static_cast<float>(value);
  } else {
    regret[index] = value;
  }
}

void ActionBuffers::add_strategy(const std::size_t index, const double value) const {
  if (paged != nullptr) {
    paged->set(count + index, paged->get(count + index) + value);
  } else if (strategy_float32 != nullptr) {
    strategy_float32[index] =
        static_cast<float>(static_cast<double>(strategy_float32[index]) + value);
  } else {
    strategy[index] += value;
  }
}

using ComboPermutation = std::array<ComboId, combo_count>;

struct RangeAutomorphism {
  SuitPermutation suits{};
  ComboPermutation combos{};
};

struct DecisionLayout {
  std::uint64_t action_base{0};
  std::uint64_t physical_infoset_base{0};
  std::uint32_t board_index{0};
  std::uint16_t action_count{0};
  std::uint8_t player{0};
  bool present{false};
};

struct CanonicalPublicOutcome {
  std::uint32_t child{0};
  CardId chance_card{};
  std::uint32_t physical_outcome_count{1};
  std::uint8_t physical_to_child_automorphism{0};
};

struct CanonicalPublicEdge {
  Action action{};
  std::vector<CanonicalPublicOutcome> outcomes;
};

struct CanonicalPublicNode {
  NodeId representative_node{0};
  PublicNodeKind kind{PublicNodeKind::Decision};
  PublicState state{};
  std::uint32_t board_index{0};
  DecisionLayout decision{};
  std::uint32_t total_legal_outcome_count{0};
  std::vector<CanonicalPublicEdge> edges;
  std::vector<std::uint16_t> update_multiplicity;
  std::array<double, 2> fold_payoff_antes{};
  std::array<std::array<double, 3>, 2> showdown_payoff_antes{};
};

struct CanonicalPublicGraph {
  std::uint32_t root{0};
  std::vector<CanonicalPublicNode> nodes;
};

struct BoardData {
  std::uint64_t mask{0};
  std::array<std::int16_t, combo_count> local_index{};
  std::vector<ComboId> legal_combos;
  // Per-player live combos: only the acting player's own range combos are
  // stored as action slots at that player's decision nodes. This halves the
  // tree for asymmetric ranges without changing any reach-weighted result,
  // since combos outside the actor's range have zero actor reach.
  std::array<std::vector<ComboId>, 2> player_combos;
  std::array<std::array<std::int16_t, combo_count>, 2> player_local{};
  std::array<std::int16_t, combo_count> rank_index{};
  std::uint16_t rank_count{0};
  bool ranks_ready{false};
};

struct DenseLayout {
  PublicTree tree;
  std::array<Combo, combo_count> combos{};
  std::array<std::uint64_t, combo_count> combo_masks{};
  std::array<DenseComboVector, 2> initial_reach{};
  std::array<std::int16_t, combo_count> active_combo_index{};
  // Per-player flop-range combo spaces: the compact value/reach vectors used
  // by the physical (non-DAG) traversal are indexed by the updating player's
  // flop-range combos (player_flop_combos[player]), which are stable across
  // every deeper board (a combo is either still live, with the same slot, or
  // blocked with zero reach). player_flop_slot[player][combo] is the slot, or
  // -1 when the combo is outside the player's flop range.
  std::array<std::array<std::int16_t, combo_count>, 2> player_flop_slot{};
  std::array<std::vector<ComboId>, 2> player_flop_combos;
  std::array<std::size_t, 2> player_flop_count{};
  std::vector<ComboId> active_combos;
  std::array<std::vector<std::uint16_t>, 36U> active_slots_by_card;
  std::array<std::vector<ComboId>, 36U> active_combos_by_card;
  std::vector<std::vector<std::uint16_t>> active_automorphism_slots;
  std::vector<BoardData> boards;
  std::vector<std::uint32_t> node_board;
  std::vector<DecisionLayout> decisions;
  std::vector<std::uint32_t> physical_infoset_ids;
  std::vector<std::uint64_t> canonical_action_bases;
  std::vector<std::uint16_t> canonical_action_counts;
  std::vector<std::uint32_t> canonical_infoset_multiplicity;
  std::vector<RangeAutomorphism> automorphisms;
  CanonicalPublicGraph canonical_public_graph;
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  double initial_normalization{0.0};
  bool uses_isomorphic_infosets{false};
  // True when the tree has only the identity automorphism (automorphisms.size()
  // <= 1): every physical infoset is unique, so each combo's action block is
  // laid out contiguously at decision.action_base + local * action_count and
  // decision_action_base can skip the two sparse canonical lookups.
  bool uses_direct_action_bases{false};
  bool uses_canonical_public_dag{false};
  std::string fingerprint;
};

struct NodeHistory {
  std::uint32_t betting_history{0};
  std::array<CardId, 2> chance_cards{};
  std::uint8_t chance_count{0};
};

struct ActionHistoryKey {
  std::uint32_t parent{0};
  std::int64_t amount_units{0};
  std::uint32_t requested_basis_points{0};
  std::uint8_t edge_kind{0};
  std::uint8_t action_type{0};
  std::uint8_t all_in_kind{0};

  friend bool operator==(const ActionHistoryKey &, const ActionHistoryKey &) = default;
};

struct ActionHistoryKeyHash {
  std::size_t operator()(const ActionHistoryKey &key) const noexcept {
    std::uint64_t hash = static_cast<std::uint64_t>(key.parent) << 32U;
    hash ^= static_cast<std::uint64_t>(key.amount_units);
    hash ^= static_cast<std::uint64_t>(key.requested_basis_points) << 8U;
    hash ^= static_cast<std::uint64_t>(key.edge_kind) << 56U;
    hash ^= static_cast<std::uint64_t>(key.action_type) << 48U;
    hash ^= static_cast<std::uint64_t>(key.all_in_kind) << 40U;
    hash ^= hash >> 33U;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33U;
    return static_cast<std::size_t>(hash);
  }
};

struct CanonicalInfosetKey {
  std::uint32_t public_history{0};
  std::uint64_t board_mask{0};
  std::uint16_t private_combo{0};
  std::uint16_t ordered_chance_cards{0};

  friend bool operator==(const CanonicalInfosetKey &, const CanonicalInfosetKey &) = default;
};

struct CanonicalInfosetKeyHash {
  std::size_t operator()(const CanonicalInfosetKey &key) const noexcept {
    std::uint64_t hash = key.board_mask ^ (static_cast<std::uint64_t>(key.public_history) << 32U);
    hash ^= static_cast<std::uint64_t>(key.private_combo) << 16U;
    hash ^= key.ordered_chance_cards;
    hash ^= hash >> 33U;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33U;
    return static_cast<std::size_t>(hash);
  }
};

struct CanonicalInfosetEntry {
  std::uint32_t id{0};
  NodeId representative_node{0};
};

struct CanonicalPublicKey {
  std::uint32_t public_history{0};
  std::uint64_t board_mask{0};
  std::uint16_t ordered_chance_cards{0};

  friend bool operator==(const CanonicalPublicKey &, const CanonicalPublicKey &) = default;
};

struct CanonicalPublicKeyHash {
  std::size_t operator()(const CanonicalPublicKey &key) const noexcept {
    std::uint64_t hash = key.board_mask ^ (static_cast<std::uint64_t>(key.public_history) << 32U);
    hash ^= key.ordered_chance_cards;
    hash ^= hash >> 33U;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33U;
    return static_cast<std::size_t>(hash);
  }
};

struct CanonicalPublicAssignment {
  std::uint32_t node{0};
  std::uint8_t physical_to_canonical_automorphism{0};
};

template <typename Value> bool finite_vector(const std::vector<Value> &values) {
  return std::all_of(values.begin(), values.end(),
                     [](const Value value) { return std::isfinite(value); });
}

std::string fingerprint_text(const std::string_view text) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::string serialize_range_fingerprint(const PostflopRanges &ranges) {
  std::string result;
  result.reserve(2U * combo_count * 6U);
  for (const auto &range : ranges.players) {
    for (const auto weight : range) {
      result += std::to_string(weight.basis_points());
      result.push_back(',');
    }
    result.push_back('|');
  }
  return result;
}

bool uniform_full_ranges(const PostflopRanges &ranges) {
  return std::all_of(ranges.players.begin(), ranges.players.end(), [](const auto &range) {
    return std::all_of(range.begin(), range.end(),
                       [](const auto weight) { return weight.basis_points() == 10'000U; });
  });
}

using ComboLookup = std::array<std::array<std::int16_t, 36>, 36>;

ComboLookup make_combo_lookup(const std::array<Combo, combo_count> &combos) {
  ComboLookup lookup{};
  for (auto &row : lookup) {
    row.fill(-1);
  }
  for (std::size_t index = 0; index < combos.size(); ++index) {
    const auto first = std::min(combos[index].first.value(), combos[index].second.value());
    const auto second = std::max(combos[index].first.value(), combos[index].second.value());
    lookup[first][second] = static_cast<std::int16_t>(index);
  }
  return lookup;
}

Result<std::vector<RangeAutomorphism>, PostflopSolverError>
range_automorphisms(const std::array<Combo, combo_count> &combos, const PostflopRanges &ranges,
                    const std::uint64_t initial_board_mask) {
  const auto lookup = make_combo_lookup(combos);
  std::vector<RangeAutomorphism> automorphisms;
  for (const auto &permutation : all_suit_permutations()) {
    const auto transformed_board = transform_card_mask(initial_board_mask, permutation);
    if (!transformed_board || transformed_board.value() != initial_board_mask) {
      continue;
    }
    ComboPermutation mapping{};
    bool invariant = true;
    for (std::size_t combo = 0; combo < combos.size(); ++combo) {
      const auto transformed = transform_combo(combos[combo], permutation);
      if (!transformed) {
        return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto first =
          std::min(transformed.value().first.value(), transformed.value().second.value());
      const auto second =
          std::max(transformed.value().first.value(), transformed.value().second.value());
      const auto transformed_id = lookup[first][second];
      if (transformed_id < 0) {
        return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      mapping[combo] = static_cast<ComboId>(transformed_id);
      for (std::size_t player = 0; player < ranges.players.size(); ++player) {
        if (ranges.players[player][combo] !=
            ranges.players[player][static_cast<std::size_t>(transformed_id)]) {
          invariant = false;
          break;
        }
      }
      if (!invariant) {
        break;
      }
    }
    if (invariant) {
      automorphisms.push_back(RangeAutomorphism{permutation, mapping});
    }
  }
  if (automorphisms.empty()) {
    return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  return Result<std::vector<RangeAutomorphism>, PostflopSolverError>::success(
      std::move(automorphisms));
}

Result<std::vector<NodeHistory>, PostflopSolverError> build_node_histories(const PublicTree &tree) {
  std::vector<NodeHistory> histories(tree.nodes.size());
  std::vector<bool> assigned(tree.nodes.size(), false);
  std::unordered_map<ActionHistoryKey, std::uint32_t, ActionHistoryKeyHash> history_ids;
  history_ids.reserve(tree.nodes.size());
  assigned[static_cast<std::size_t>(tree.root)] = true;
  for (const auto &node : tree.nodes) {
    if (!assigned[static_cast<std::size_t>(node.id)]) {
      return Result<std::vector<NodeHistory>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    for (const auto &edge : node.edges) {
      if (edge.child >= histories.size()) {
        return Result<std::vector<NodeHistory>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      auto child = histories[static_cast<std::size_t>(node.id)];
      const ActionHistoryKey history_key{
          child.betting_history,
          edge.kind == PublicEdgeKind::Action ? edge.action.amount.units() : 0,
          edge.kind == PublicEdgeKind::Action ? edge.action.requested_basis_points : 0U,
          static_cast<std::uint8_t>(edge.kind),
          edge.kind == PublicEdgeKind::Action ? static_cast<std::uint8_t>(edge.action.type) : 0U,
          edge.kind == PublicEdgeKind::Action ? static_cast<std::uint8_t>(edge.action.all_in_kind)
                                              : 0U};
      const auto next_history_id = static_cast<std::uint32_t>(history_ids.size() + 1U);
      child.betting_history = history_ids.emplace(history_key, next_history_id).first->second;
      if (edge.kind == PublicEdgeKind::ChanceCard) {
        if (child.chance_count >= child.chance_cards.size()) {
          return Result<std::vector<NodeHistory>, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        child.chance_cards[child.chance_count++] = edge.chance_card;
      }
      histories[static_cast<std::size_t>(edge.child)] = std::move(child);
      assigned[static_cast<std::size_t>(edge.child)] = true;
    }
  }
  return Result<std::vector<NodeHistory>, PostflopSolverError>::success(std::move(histories));
}

std::vector<std::uint32_t> intern_public_histories(const PublicTree &tree,
                                                   const std::vector<NodeHistory> &histories) {
  std::unordered_map<std::string, std::uint32_t> ids;
  std::vector<std::uint32_t> result(tree.nodes.size());
  for (const auto &node : tree.nodes) {
    auto state = node.state;
    state.board_mask = 0U;
    auto signature = serialize_public_state(state);
    signature.push_back('|');
    signature += std::to_string(histories[static_cast<std::size_t>(node.id)].betting_history);
    const auto next_id = static_cast<std::uint32_t>(ids.size());
    result[static_cast<std::size_t>(node.id)] =
        ids.emplace(std::move(signature), next_id).first->second;
  }
  return result;
}

bool same_actions(const PublicTreeNode &left, const PublicTreeNode &right) {
  if (left.edges.size() != right.edges.size()) {
    return false;
  }
  for (std::size_t action = 0; action < left.edges.size(); ++action) {
    if (left.edges[action].kind != PublicEdgeKind::Action ||
        right.edges[action].kind != PublicEdgeKind::Action ||
        left.edges[action].action != right.edges[action].action) {
      return false;
    }
  }
  return true;
}

std::uint16_t transformed_ordered_chance_cards(const NodeHistory &history,
                                               const SuitPermutation &permutation) {
  std::uint16_t packed = static_cast<std::uint16_t>(history.chance_count) << 12U;
  for (std::size_t index = 0; index < history.chance_count; ++index) {
    const auto transformed = transform_card(history.chance_cards[index], permutation).value();
    packed |= static_cast<std::uint16_t>(transformed.value()) << (index == 0U ? 6U : 0U);
  }
  return packed;
}

Result<CanonicalPublicGraph, PostflopSolverError>
build_canonical_public_graph(const PublicTree &tree, const std::vector<NodeHistory> &histories,
                             const std::vector<std::uint32_t> &public_history_ids,
                             const std::vector<RangeAutomorphism> &automorphisms) {
  if (histories.size() != tree.nodes.size() || public_history_ids.size() != tree.nodes.size() ||
      automorphisms.empty() || automorphisms.size() > 24U) {
    return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  std::size_t identity = automorphisms.size();
  const SuitPermutation identity_permutation{};
  for (std::size_t index = 0; index < automorphisms.size(); ++index) {
    if (automorphisms[index].suits == identity_permutation) {
      identity = index;
      break;
    }
  }
  if (identity == automorphisms.size()) {
    return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  CanonicalPublicGraph graph;
  std::vector<CanonicalPublicAssignment> assignments(tree.nodes.size());
  std::unordered_map<CanonicalPublicKey, std::uint32_t, CanonicalPublicKeyHash> canonical_nodes;
  canonical_nodes.reserve(tree.nodes.size() / automorphisms.size() + 1U);
  for (const auto &physical : tree.nodes) {
    CanonicalPublicKey canonical{};
    std::size_t selected_automorphism = 0U;
    bool has_canonical = false;
    for (std::size_t index = 0; index < automorphisms.size(); ++index) {
      const auto board = transform_card_mask(physical.state.board_mask, automorphisms[index].suits);
      if (!board) {
        return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const CanonicalPublicKey candidate{
          public_history_ids[static_cast<std::size_t>(physical.id)], board.value(),
          transformed_ordered_chance_cards(histories[static_cast<std::size_t>(physical.id)],
                                           automorphisms[index].suits)};
      if (!has_canonical ||
          std::tie(candidate.public_history, candidate.board_mask, candidate.ordered_chance_cards) <
              std::tie(canonical.public_history, canonical.board_mask,
                       canonical.ordered_chance_cards)) {
        canonical = candidate;
        selected_automorphism = index;
        has_canonical = true;
      }
    }
    const auto next_id = static_cast<std::uint32_t>(graph.nodes.size());
    const auto [found, inserted] = canonical_nodes.emplace(canonical, next_id);
    if (inserted) {
      CanonicalPublicNode node;
      node.representative_node = std::numeric_limits<NodeId>::max();
      node.kind = physical.kind;
      graph.nodes.push_back(std::move(node));
    } else if (graph.nodes[found->second].kind != physical.kind) {
      return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }

    const auto physical_chance = transformed_ordered_chance_cards(
        histories[static_cast<std::size_t>(physical.id)], automorphisms[identity].suits);
    if (physical.state.board_mask == canonical.board_mask &&
        physical_chance == canonical.ordered_chance_cards) {
      selected_automorphism = identity;
      graph.nodes[found->second].representative_node = physical.id;
    }
    assignments[static_cast<std::size_t>(physical.id)] = {
        found->second, static_cast<std::uint8_t>(selected_automorphism)};
  }

  for (const auto &node : graph.nodes) {
    if (node.representative_node == std::numeric_limits<NodeId>::max()) {
      return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
  }
  graph.root = assignments[static_cast<std::size_t>(tree.root)].node;

  for (auto &canonical_node : graph.nodes) {
    const auto &physical = tree.nodes[static_cast<std::size_t>(canonical_node.representative_node)];
    if (physical.kind == PublicNodeKind::Decision) {
      canonical_node.edges.reserve(physical.edges.size());
      for (const auto &edge : physical.edges) {
        if (edge.kind != PublicEdgeKind::Action) {
          return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        const auto child = assignments[static_cast<std::size_t>(edge.child)];
        if (tree.nodes[static_cast<std::size_t>(graph.nodes[child.node].representative_node)]
                .depth <= physical.depth) {
          return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        CanonicalPublicEdge canonical_edge;
        canonical_edge.action = edge.action;
        canonical_edge.outcomes.push_back(
            {child.node, CardId{}, 1U, child.physical_to_canonical_automorphism});
        canonical_node.edges.push_back(std::move(canonical_edge));
      }
    } else if (physical.kind == PublicNodeKind::Chance) {
      std::unordered_map<std::uint32_t, std::size_t> grouped_children;
      grouped_children.reserve(physical.edges.size());
      for (const auto &edge : physical.edges) {
        if (edge.kind != PublicEdgeKind::ChanceCard) {
          return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        const auto child = assignments[static_cast<std::size_t>(edge.child)];
        if (tree.nodes[static_cast<std::size_t>(graph.nodes[child.node].representative_node)]
                .depth <= physical.depth) {
          return Result<CanonicalPublicGraph, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        const auto next_edge = canonical_node.edges.size();
        const auto [group, inserted] = grouped_children.emplace(child.node, next_edge);
        if (inserted) {
          canonical_node.edges.emplace_back();
        }
        canonical_node.edges[group->second].outcomes.push_back(
            {child.node, edge.chance_card, edge.physical_outcome_count,
             child.physical_to_canonical_automorphism});
      }
    }
  }
  return Result<CanonicalPublicGraph, PostflopSolverError>::success(std::move(graph));
}

std::uint64_t decision_action_base(const DenseLayout &layout, const DecisionLayout &decision,
                                   const std::int16_t local_combo) {
  if (layout.uses_direct_action_bases) {
    return decision.action_base + static_cast<std::uint64_t>(local_combo) * decision.action_count;
  }
  if (!layout.uses_isomorphic_infosets) {
    return decision.action_base + static_cast<std::uint64_t>(local_combo) * decision.action_count;
  }
  const auto infoset_id = layout.physical_infoset_ids[decision.physical_infoset_base +
                                                      static_cast<std::uint64_t>(local_combo)];
  return layout.canonical_action_bases[infoset_id];
}

std::uint32_t decision_infoset_id(const DenseLayout &layout, const DecisionLayout &decision,
                                  const std::int16_t local_combo) {
  return layout.physical_infoset_ids[decision.physical_infoset_base +
                                     static_cast<std::uint64_t>(local_combo)];
}

std::vector<CardId> cards_from_mask(const std::uint64_t mask) {
  std::vector<CardId> cards;
  cards.reserve(static_cast<std::size_t>(std::popcount(mask)));
  for (std::uint8_t index = 0; index < 36U; ++index) {
    if ((mask & (std::uint64_t{1} << index)) != 0U) {
      cards.push_back(CardId::from_index(index).value());
    }
  }
  return cards;
}

Result<DenseLayout, PostflopSolverError>
build_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges,
             const bool enable_lossless_isomorphism = true,
             const bool enable_canonical_public_dag = false) {
  TreeBuildOptions options;
  options.maximum_nodes = std::numeric_limits<std::uint64_t>::max();
  auto tree = build_public_tree(config, options);
  if (!tree) {
    return Result<DenseLayout, PostflopSolverError>::failure(PostflopSolverError::TreeFailure);
  }

  DenseLayout layout;
  layout.tree = std::move(tree.value());
  layout.active_combo_index.fill(-1);  layout.combos = all_combos();
  for (std::size_t combo = 0; combo < combo_count; ++combo) {
    layout.combo_masks[combo] =
        layout.combos[combo].first.mask() | layout.combos[combo].second.mask();
  }
  layout.node_board.resize(layout.tree.nodes.size());
  layout.decisions.resize(layout.tree.nodes.size());
  std::unordered_map<std::uint64_t, std::uint32_t> board_lookup;

  for (const auto &node : layout.tree.nodes) {
    auto found = board_lookup.find(node.state.board_mask);
    if (found == board_lookup.end()) {
      BoardData board;
      board.mask = node.state.board_mask;
      board.local_index.fill(-1);
      board.rank_index.fill(-1);
      for (std::size_t combo = 0; combo < combo_count; ++combo) {
        const bool present_in_source_range = ranges.players[0][combo].basis_points() != 0U ||
                                             ranges.players[1][combo].basis_points() != 0U;
        if (present_in_source_range && (layout.combo_masks[combo] & board.mask) == 0U) {
          board.local_index[combo] = static_cast<std::int16_t>(board.legal_combos.size());
          board.legal_combos.push_back(static_cast<ComboId>(combo));
        }
        for (std::size_t player = 0; player < 2U; ++player) {
          if (ranges.players[player][combo].basis_points() != 0U &&
              (layout.combo_masks[combo] & board.mask) == 0U) {
            board.player_local[player][combo] =
                static_cast<std::int16_t>(board.player_combos[player].size());
            board.player_combos[player].push_back(static_cast<ComboId>(combo));
          }
        }
      }
      const auto board_index = static_cast<std::uint32_t>(layout.boards.size());
      layout.boards.push_back(std::move(board));
      found = board_lookup.emplace(node.state.board_mask, board_index).first;
    }
    layout.node_board[static_cast<std::size_t>(node.id)] = found->second;
  }

  layout.uses_isomorphic_infosets = enable_lossless_isomorphism && !uniform_full_ranges(ranges);
  std::vector<NodeHistory> histories;
  std::vector<std::uint32_t> public_history_ids;
  std::vector<RangeAutomorphism> automorphisms;
  std::unordered_map<CanonicalInfosetKey, CanonicalInfosetEntry, CanonicalInfosetKeyHash>
      canonical_infosets;
  if (layout.uses_isomorphic_infosets) {
    // Automorphisms first: the per-player direct-action-bases path (identity
    // automorphism group only) never uses the canonical infoset map or the
    // canonical arrays (decision_action_base computes the offset directly from
    // the local combo index), so skip the histories/interning/canonicalization
    // entirely for it. For th7d6s this avoids a ~2.5 GB transient
    // unordered_map (36.6M canonical infosets) and ~0.5 GB of unused arrays,
    // cutting the peak RSS from ~4.3 GB to ~1.5-2 GB.
    const auto initial_board =
        layout.tree.nodes[static_cast<std::size_t>(layout.tree.root)].state.board_mask;
    auto exact_automorphisms = range_automorphisms(layout.combos, ranges, initial_board);
    if (!exact_automorphisms) {
      return Result<DenseLayout, PostflopSolverError>::failure(exact_automorphisms.error());
    }
    automorphisms = std::move(exact_automorphisms.value());
    layout.uses_direct_action_bases = automorphisms.size() <= 1U;
    if (!layout.uses_direct_action_bases) {
      auto built_histories = build_node_histories(layout.tree);
      if (!built_histories) {
        return Result<DenseLayout, PostflopSolverError>::failure(built_histories.error());
      }
      histories = std::move(built_histories.value());
      public_history_ids = intern_public_histories(layout.tree, histories);
      canonical_infosets.reserve(layout.tree.stats.decision_nodes * 8U);
    }
  }

  for (const auto &node : layout.tree.nodes) {
    if (node.kind != PublicNodeKind::Decision) {
      continue;
    }
    auto &decision = layout.decisions[static_cast<std::size_t>(node.id)];
    decision.board_index = layout.node_board[static_cast<std::size_t>(node.id)];
    decision.action_count = static_cast<std::uint16_t>(node.edges.size());
    decision.player = node.state.player_to_act;
    decision.present = true;
    const auto &board = layout.boards[decision.board_index];
    const auto legal_count = board.player_combos[decision.player].size();
    if (decision.action_count == 0U || decision.action_count > maximum_action_count) {
      return Result<DenseLayout, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    if (!layout.uses_isomorphic_infosets || layout.uses_direct_action_bases) {
      // Simple per-combo action blocks: for the identity-only automorphism
      // group every combo is its own infoset and the canonical infoset
      // machinery (map, canonical arrays, physical_infoset_ids) is never read
      // by this path, so the counters match the canonical construction
      // (legal_count infosets, legal_count * action_count actions) without
      // building any of it.
      if (legal_count >
          (std::numeric_limits<std::uint64_t>::max() - layout.actions) / decision.action_count) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      decision.action_base = layout.actions;
      layout.information_sets += legal_count;
      layout.actions += legal_count * decision.action_count;
      continue;
    }

    decision.physical_infoset_base = layout.physical_infoset_ids.size();
    std::array<std::uint64_t, 24> transformed_board_masks{};
    std::array<std::uint16_t, 24> transformed_chance_histories{};
    for (std::size_t index = 0; index < automorphisms.size(); ++index) {
      const auto transformed_board = transform_card_mask(board.mask, automorphisms[index].suits);
      if (!transformed_board) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      transformed_board_masks[index] = transformed_board.value();
      transformed_chance_histories[index] = transformed_ordered_chance_cards(
          histories[static_cast<std::size_t>(node.id)], automorphisms[index].suits);
    }
    for (const ComboId combo : board.player_combos[decision.player]) {
      CanonicalInfosetKey canonical{};
      bool has_canonical = false;
      for (std::size_t index = 0; index < automorphisms.size(); ++index) {
        const CanonicalInfosetKey candidate{
            public_history_ids[static_cast<std::size_t>(node.id)], transformed_board_masks[index],
            automorphisms[index].combos[combo], transformed_chance_histories[index]};
        if (!has_canonical ||
            std::tie(candidate.public_history, candidate.board_mask, candidate.private_combo,
                     candidate.ordered_chance_cards) <
                std::tie(canonical.public_history, canonical.board_mask, canonical.private_combo,
                         canonical.ordered_chance_cards)) {
          canonical = candidate;
          has_canonical = true;
        }
      }
      const auto next_id = static_cast<std::uint32_t>(layout.canonical_action_bases.size());
      const auto [found, inserted] =
          canonical_infosets.emplace(canonical, CanonicalInfosetEntry{next_id, node.id});
      if (inserted) {
        if (layout.actions > std::numeric_limits<std::uint64_t>::max() - decision.action_count) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        layout.canonical_action_bases.push_back(layout.actions);
        layout.canonical_action_counts.push_back(decision.action_count);
        layout.canonical_infoset_multiplicity.push_back(0U);
        layout.actions += decision.action_count;
        ++layout.information_sets;
      } else if (!same_actions(node, layout.tree.nodes[static_cast<std::size_t>(
                                         found->second.representative_node)])) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      layout.physical_infoset_ids.push_back(found->second.id);
      ++layout.canonical_infoset_multiplicity[found->second.id];
    }
  }
  canonical_infosets.clear();
  canonical_infosets.rehash(0U);
  if (layout.uses_isomorphic_infosets && enable_canonical_public_dag && automorphisms.size() > 1U) {
    auto canonical_graph =
        build_canonical_public_graph(layout.tree, histories, public_history_ids, automorphisms);
    if (!canonical_graph) {
      return Result<DenseLayout, PostflopSolverError>::failure(canonical_graph.error());
    }
    for (auto &canonical_node : canonical_graph.value().nodes) {
      const auto &representative =
          layout.tree.nodes[static_cast<std::size_t>(canonical_node.representative_node)];
      canonical_node.state = representative.state;
      canonical_node.board_index =
          layout.node_board[static_cast<std::size_t>(canonical_node.representative_node)];
      if (!representative.edges.empty()) {
        canonical_node.total_legal_outcome_count =
            representative.edges.front().total_legal_outcome_count;
      }
      if (canonical_node.kind == PublicNodeKind::TerminalFold) {
        const auto settlement = settle_terminal(canonical_node.state, layout.tree.config.rake);
        if (!settlement) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::SettlementFailure);
        }
        for (std::uint8_t player = 0; player < 2U; ++player) {
          canonical_node.fold_payoff_antes[player] =
              static_cast<double>(settlement.value().payoff_units[player]) / units_per_ante;
        }
      } else if (canonical_node.kind == PublicNodeKind::TerminalShowdown) {
        const std::array<std::uint8_t, 3> winner_masks{0b10U, 0b11U, 0b01U};
        for (std::size_t outcome = 0; outcome < winner_masks.size(); ++outcome) {
          const auto settlement =
              settle_terminal(canonical_node.state, layout.tree.config.rake, winner_masks[outcome]);
          if (!settlement) {
            return Result<DenseLayout, PostflopSolverError>::failure(
                PostflopSolverError::SettlementFailure);
          }
          canonical_node.showdown_payoff_antes[0][outcome] =
              static_cast<double>(settlement.value().payoff_units[0]) / units_per_ante;
          canonical_node.showdown_payoff_antes[1][2U - outcome] =
              static_cast<double>(settlement.value().payoff_units[1]) / units_per_ante;
        }
      }
      if (canonical_node.kind != PublicNodeKind::Decision) {
        continue;
      }
      const auto &decision =
          layout.decisions[static_cast<std::size_t>(canonical_node.representative_node)];
      canonical_node.decision = decision;
      const auto &board = layout.boards[decision.board_index];
      canonical_node.update_multiplicity.resize(board.player_combos[decision.player].size(), 0U);
      for (const ComboId combo : board.player_combos[decision.player]) {
        const auto local = board.player_local[decision.player][combo];
        const auto infoset_id = decision_infoset_id(layout, decision, local);
        std::uint32_t representative_private_multiplicity = 0U;
        for (const ComboId representative_combo : board.player_combos[decision.player]) {
          if (decision_infoset_id(
                  layout, decision,
                  board.player_local[decision.player][representative_combo]) == infoset_id) {
            ++representative_private_multiplicity;
          }
        }
        if (representative_private_multiplicity == 0U ||
            layout.canonical_infoset_multiplicity[infoset_id] %
                    representative_private_multiplicity !=
                0U) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        const auto multiplicity =
            layout.canonical_infoset_multiplicity[infoset_id] / representative_private_multiplicity;
        if (multiplicity > std::numeric_limits<std::uint16_t>::max()) {
          return Result<DenseLayout, PostflopSolverError>::failure(
              PostflopSolverError::InvalidConfiguration);
        }
        canonical_node.update_multiplicity[static_cast<std::size_t>(local)] =
            static_cast<std::uint16_t>(multiplicity);
      }
    }
    layout.canonical_public_graph = std::move(canonical_graph.value());
    layout.automorphisms = automorphisms;
    layout.uses_canonical_public_dag = true;
  }
  if (layout.actions > std::numeric_limits<std::size_t>::max()) {
    return Result<DenseLayout, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  const auto &flop_board = layout.boards[layout.node_board[layout.tree.root]];
  layout.active_combos = flop_board.legal_combos;
  for (std::size_t index = 0; index < layout.active_combos.size(); ++index) {
    layout.active_combo_index[layout.active_combos[index]] = static_cast<std::int16_t>(index);
    const auto &combo = layout.combos[layout.active_combos[index]];
    layout.active_slots_by_card[combo.first.value()].push_back(static_cast<std::uint16_t>(index));
    layout.active_slots_by_card[combo.second.value()].push_back(static_cast<std::uint16_t>(index));
    layout.active_combos_by_card[combo.first.value()].push_back(layout.active_combos[index]);
    layout.active_combos_by_card[combo.second.value()].push_back(layout.active_combos[index]);
  }
  layout.active_automorphism_slots.reserve(layout.automorphisms.size());
  for (const auto &automorphism : layout.automorphisms) {
    std::vector<std::uint16_t> slots;
    slots.reserve(layout.active_combos.size());
    for (const ComboId combo : layout.active_combos) {
      const auto mapped = layout.active_combo_index[automorphism.combos[combo]];
      if (mapped < 0) {
        return Result<DenseLayout, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      slots.push_back(static_cast<std::uint16_t>(mapped));
    }
    layout.active_automorphism_slots.push_back(std::move(slots));
  }
  for (const ComboId first : flop_board.legal_combos) {
    layout.initial_reach[0][first] =
        static_cast<double>(ranges.players[0][first].basis_points()) / 10'000.0;
    layout.initial_reach[1][first] =
        static_cast<double>(ranges.players[1][first].basis_points()) / 10'000.0;
    for (const ComboId second : flop_board.legal_combos) {
      if ((layout.combo_masks[first] & layout.combo_masks[second]) == 0U) {
        layout.initial_normalization +=
            layout.initial_reach[0][first] *
            (static_cast<double>(ranges.players[1][second].basis_points()) / 10'000.0);
      }
    }
  }
  if (!(layout.initial_normalization > 0.0)) {
    return Result<DenseLayout, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  for (auto &player_slots : layout.player_flop_slot) {
    player_slots.fill(-1);
  }
  for (std::uint8_t player = 0; player < 2U; ++player) {
    layout.player_flop_combos[player] = flop_board.player_combos[player];
    layout.player_flop_count[player] = flop_board.player_combos[player].size();
    for (std::size_t index = 0; index < flop_board.player_combos[player].size(); ++index) {
      layout.player_flop_slot[player][flop_board.player_combos[player][index]] =
          static_cast<std::int16_t>(index);
    }
  }
  std::string fingerprint_source =
      layout.tree.betting_tree_hash + "|" + serialize_tree_config_json(config);
  if (!uniform_full_ranges(ranges)) {
    fingerprint_source +=
        "|ranges-v1|" + serialize_range_fingerprint(ranges) +
        (layout.uses_isomorphic_infosets ? "|iso-infosets-v1" : "|physical-infosets-v1");
  }
  layout.fingerprint = fingerprint_text(fingerprint_source);
  if (layout.uses_canonical_public_dag) {
    layout.tree.nodes.clear();
    layout.tree.nodes.shrink_to_fit();
    layout.node_board.clear();
    layout.node_board.shrink_to_fit();
    layout.decisions.clear();
    layout.decisions.shrink_to_fit();
  }
  return Result<DenseLayout, PostflopSolverError>::success(std::move(layout));
}

Result<bool, PostflopSolverError> prepare_ranks(DenseLayout &layout,
                                                const std::uint32_t board_index) {
  auto &board = layout.boards[board_index];
  if (board.ranks_ready) {
    return Result<bool, PostflopSolverError>::success(true);
  }
  if (std::popcount(board.mask) != 5) {
    return Result<bool, PostflopSolverError>::failure(PostflopSolverError::EquityFailure);
  }
  const auto board_cards = cards_from_mask(board.mask);
  std::vector<std::pair<HandValue, ComboId>> ranked;
  ranked.reserve(board.legal_combos.size());
  for (const ComboId combo_id : board.legal_combos) {
    const auto &combo = layout.combos[combo_id];
    std::array<CardId, 7> cards{board_cards[0], board_cards[1], board_cards[2], board_cards[3],
                                board_cards[4], combo.first,    combo.second};
    const auto value = evaluate_seven(cards);
    if (!value) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::EquityFailure);
    }
    ranked.emplace_back(value.value(), combo_id);
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &left, const auto &right) { return left.first < right.first; });
  std::int16_t rank = -1;
  HandValue previous{};
  bool has_previous = false;
  for (const auto &[value, combo_id] : ranked) {
    if (!has_previous || value != previous) {
      ++rank;
      previous = value;
      has_previous = true;
    }
    board.rank_index[combo_id] = rank;
  }
  board.rank_count = static_cast<std::uint16_t>(rank + 1);
  board.ranks_ready = true;
  return Result<bool, PostflopSolverError>::success(true);
}

std::string root_action_label(const Action &action) {
  std::string label;
  switch (action.type) {
  case ActionType::Fold:
    label = "fold";
    break;
  case ActionType::Check:
    label = "check";
    break;
  case ActionType::Call:
    label = "call";
    break;
  case ActionType::Bet:
    label = "bet";
    break;
  case ActionType::Raise:
    label = "raise";
    break;
  case ActionType::AllIn:
    label = "all_in";
    break;
  }
  if (action.amount.units() > 0) {
    label += "_" + std::to_string(action.amount.units() / gtosd::Money::units_per_ante);
  }
  return label;
}

struct PreparedRootLock {
  std::string source_description;
  double source_dev_percent{0.0};
  std::vector<std::optional<std::array<double, maximum_action_count>>> by_local_combo;
};

Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>
prepare_root_lock(const DiagnosticRootLock &lock, const DenseLayout &layout) {
  // The root is read from the canonical public graph when the DAG is enabled
  // (the physical tree and decision table are not retained in that layout),
  // otherwise from the physical tree.
  std::vector<std::string> root_labels;
  std::uint16_t root_action_count = 0;
  std::uint32_t root_board_index = 0;
  std::uint8_t root_player = 0;
  bool root_is_decision = false;
  if (layout.uses_canonical_public_dag) {
    const auto &root_node =
        layout.canonical_public_graph.nodes[layout.canonical_public_graph.root];
    root_is_decision = root_node.kind == PublicNodeKind::Decision;
    root_player = root_node.state.player_to_act;
    root_board_index = root_node.board_index;
    root_action_count = root_node.decision.action_count;
    for (const auto &edge : root_node.edges) {
      root_labels.push_back(root_action_label(edge.action));
    }
  } else {
    const auto &root_node = layout.tree.nodes[static_cast<std::size_t>(layout.tree.root)];
    root_is_decision = root_node.kind == PublicNodeKind::Decision;
    root_player = root_node.state.player_to_act;
    const auto &decision = layout.decisions[static_cast<std::size_t>(layout.tree.root)];
    root_board_index = decision.board_index;
    root_action_count = decision.action_count;
    for (const auto &edge : root_node.edges) {
      if (edge.kind == PublicEdgeKind::ChanceCard) {
        continue;
      }
      root_labels.push_back(root_action_label(edge.action));
    }
  }
  if (!root_is_decision || root_player != 0U || root_action_count == 0U) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &board = layout.boards[root_board_index];
  if (board.legal_combos.size() != lock.entries.size()) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  if (root_labels.size() != root_action_count) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  auto labels_sorted = root_labels;
  std::ranges::sort(labels_sorted);

  auto prepared = std::make_unique<PreparedRootLock>();
  prepared->source_description = lock.source_description;
  prepared->source_dev_percent = lock.source_dev_percent;
  prepared->by_local_combo.resize(board.legal_combos.size());

  std::vector<bool> covered(board.legal_combos.size(), false);
  for (const auto &entry : lock.entries) {
    if (entry.action_labels.size() != entry.probabilities.size() ||
        entry.probabilities.size() != root_action_count) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto combo_it = std::ranges::find(layout.combos, entry.combo);
    if (combo_it == layout.combos.end()) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto combo_id = static_cast<ComboId>(std::distance(layout.combos.begin(), combo_it));
    const auto local = board.local_index[combo_id];
    if (local < 0 || static_cast<std::size_t>(local) >= covered.size() ||
        covered[static_cast<std::size_t>(local)]) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    auto entry_labels = entry.action_labels;
    std::ranges::sort(entry_labels);
    if (entry_labels != labels_sorted) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    std::array<double, maximum_action_count> probabilities{};
    double sum = 0.0;
    for (std::size_t action = 0; action < root_action_count; ++action) {
      const auto it = std::ranges::find(entry.action_labels, root_labels[action]);
      if (it == entry.action_labels.end()) {
        return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      const auto probability =
          entry.probabilities[static_cast<std::size_t>(it - entry.action_labels.begin())];
      if (!std::isfinite(probability) || probability < 0.0) {
        return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      probabilities[action] = probability;
      sum += probability;
    }
    if (std::abs(sum - 1.0) > 1.0e-9) {
      return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    prepared->by_local_combo[static_cast<std::size_t>(local)] = probabilities;
    covered[static_cast<std::size_t>(local)] = true;
  }
  if (std::ranges::any_of(covered, [](const bool present) { return !present; })) {
    return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  return Result<std::unique_ptr<PreparedRootLock>, PostflopSolverError>::success(
      std::move(prepared));
}

template <std::size_t Capacity, bool PlayerIndexed = false> class DenseTraversal {
  using ComboVector = TraversalComboVector<Capacity>;
  using TraversalResult = Result<ComboVector, PostflopSolverError>;
  // Reach passed down the tree as two pointers (plan: zero opponent reach
  // copies): the actor's vector is a per-depth scratch (only its flop-range
  // prefix is written), the opponent's vector is shared from the parent.
  using ReachRef = std::array<const ComboVector *, 2>;

  // Tag for constructing a pool worker: a full traversal that owns its own
  // deferred regret delta and worker thread but no nested workers.
  struct LeafWorkerTag {};

  // Pull-based shared task queue: all pool threads drain the same queue, so
  // tasks dispatched below the turn chance (river split) are picked up by any
  // idle worker and the pool self-balances. Teardown wakes every waiter
  // (notify_all) because the shared queue has one condition variable for all
  // workers; a single notify_one would leave the other workers blocked and
  // hang the destructor join.
  struct ParallelTaskQueue {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
    bool shutdown{false};
  };

  void run_worker_loop() {
    ParallelTaskQueue *const queue = parallel_shared_.get();
    while (true) {
      std::optional<std::packaged_task<TraversalResult(DenseTraversal &)>> task;
      {
        std::unique_lock lock(queue->mutex);
        queue->ready.wait(lock, [&] { return queue->shutdown || !queue->tasks.empty(); });
        if (queue->shutdown && queue->tasks.empty()) {
          return;
        }
        task = std::move(queue->tasks.front());
        queue->tasks.pop_front();
      }
      (*task)(*this);
    }
  }

public:
  DenseTraversal(DenseLayout &layout, const ActionBuffers buffers,
                 std::vector<double> *deferred_regret_delta = nullptr,
                 const std::uint8_t parallel_action_depth = 0U,
                 const PreparedRootLock *root_lock = nullptr)
      : layout_(layout), buffers_(buffers), deferred_regret_delta_(deferred_regret_delta),
        root_lock_(root_lock) {
    if (deferred_regret_delta_ != nullptr) {
      deferred_regret_touched_flags_.resize(deferred_regret_delta_->size(), 0U);
    }
    if (parallel_action_depth > 0U) {
      if (layout_.uses_canonical_public_dag) {
        // Canonical DAG: single-worker chain used by the decision-level
        // dispatch (the lossless DAG already shrinks the tree, so one extra
        // worker is enough). Requires a deferred delta for the parallel
        // regret merge.
        if (deferred_regret_delta_ == nullptr) {
          return;
        }
        parallel_regret_delta_.resize(deferred_regret_delta_->size(), 0.0);
        parallel_shared_ = std::make_shared<ParallelTaskQueue>();
        parallel_worker_ =
            std::make_unique<DenseTraversal>(layout_, buffers_, &parallel_regret_delta_,
                                             static_cast<std::uint8_t>(parallel_action_depth - 1U),
                                             root_lock_);
        parallel_thread_ = std::jthread([this] { run_worker_loop(); });
      } else {
        // Physical tree: a pool of independent workers, each with its own
        // deferred regret delta when accumulating regrets (solve), or with a
        // null delta when evaluating a fixed profile (certification — the
        // policy traversal never touches regret state), used by the coarse
        // chance-node split so the per-card subtrees run concurrently.
        const std::size_t worker_count = static_cast<std::size_t>(parallel_action_depth);
        const bool has_deltas = deferred_regret_delta_ != nullptr;
        if (has_deltas) {
          parallel_worker_deltas_.resize(worker_count);
          for (auto &delta : parallel_worker_deltas_) {
            delta.resize(deferred_regret_delta_->size(), 0.0);
          }
        }
        parallel_shared_ = std::make_shared<ParallelTaskQueue>();
        parallel_workers_.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
          parallel_workers_.push_back(std::make_unique<DenseTraversal>(
              LeafWorkerTag{}, layout_, buffers_,
              has_deltas ? &parallel_worker_deltas_[index] : nullptr, root_lock_,
              parallel_shared_));
        }
      }
    }
  }

  // Pool worker constructor: owns a thread that drains the shared task queue,
  // but creates no nested workers.
  DenseTraversal(LeafWorkerTag, DenseLayout &layout, const ActionBuffers buffers,
                 std::vector<double> *deferred_regret_delta, const PreparedRootLock *root_lock,
                 std::shared_ptr<ParallelTaskQueue> shared_queue)
      : layout_(layout), buffers_(buffers), deferred_regret_delta_(deferred_regret_delta),
        root_lock_(root_lock), parallel_shared_(std::move(shared_queue)) {
    if (deferred_regret_delta_ != nullptr) {
      deferred_regret_touched_flags_.resize(deferred_regret_delta_->size(), 0U);
    }
    parallel_thread_ = std::jthread([this] { run_worker_loop(); });
  }

  ~DenseTraversal() {
    if (parallel_thread_.joinable()) {
      ParallelTaskQueue *const queue = parallel_shared_.get();
      {
        std::scoped_lock lock(queue->mutex);
        queue->shutdown = true;
      }
      queue->ready.notify_all();
    }
  }

  // Per-node timing instrumentation (GTOSD_PROFILE_HOTPATH=1): accumulated
  // on every thread that runs cfr_decision, so totals are serial-equivalent.
  mutable std::uint64_t prof_decisions_ = 0;
  mutable std::uint64_t prof_actor_writes_ = 0;
  mutable double prof_strategy_seconds_ = 0.0;
  mutable double prof_copy_seconds_ = 0.0;
  mutable double prof_children_seconds_ = 0.0;
  mutable double prof_value_update_seconds_ = 0.0;
  mutable double prof_terminal_seconds_ = 0.0;
  mutable double prof_chance_seconds_ = 0.0;
  mutable double prof_sync_seconds_ = 0.0;
  mutable double prof_wall_seconds_ = 0.0;

  // Per-pass profile (GTOSD_PROFILE_HOTPATH=1): sums this traversal's and its
  // pool workers' counters (serial-equivalent), prints a per-pass breakdown
  // and resets all counters. The residual (pass wall minus the accounted
  // parts) is the pure recursion/dispatch overhead of the CFR walk.
  void dump_per_pass_profile(const double wall_seconds) {
    bool profile = false;
    {
#pragma warning(push)
#pragma warning(disable : 4996)
      profile = std::getenv("GTOSD_PROFILE_HOTPATH") != nullptr;
#pragma warning(pop)
    }
    if (!profile) {
      return;
    }
    double strategy = prof_strategy_seconds_;
    double copy = prof_copy_seconds_;
    double terminal = prof_terminal_seconds_;
    double value_update = prof_value_update_seconds_;
    double chance = prof_chance_seconds_;
    double sync = prof_sync_seconds_;
    double wall = prof_wall_seconds_;
    std::uint64_t decisions = prof_decisions_;
    std::uint64_t actor_writes = prof_actor_writes_;
    for (const auto &worker : parallel_workers_) {
      strategy += worker->prof_strategy_seconds_;
      copy += worker->prof_copy_seconds_;
      terminal += worker->prof_terminal_seconds_;
      value_update += worker->prof_value_update_seconds_;
      chance += worker->prof_chance_seconds_;
      sync += worker->prof_sync_seconds_;
      wall += worker->prof_wall_seconds_;
      decisions += worker->prof_decisions_;
      actor_writes += worker->prof_actor_writes_;
    }
    if (parallel_worker_ != nullptr) {
      strategy += parallel_worker_->prof_strategy_seconds_;
      copy += parallel_worker_->prof_copy_seconds_;
      terminal += parallel_worker_->prof_terminal_seconds_;
      value_update += parallel_worker_->prof_value_update_seconds_;
      chance += parallel_worker_->prof_chance_seconds_;
      sync += parallel_worker_->prof_sync_seconds_;
      wall += parallel_worker_->prof_wall_seconds_;
      decisions += parallel_worker_->prof_decisions_;
      actor_writes += parallel_worker_->prof_actor_writes_;
    }
    const double accounted = strategy + copy + terminal + value_update + chance + sync;
    std::fprintf(stderr,
                 "ITER-PROF wall=%.1fms decisions=%llu actor_writes=%llu serial-equiv-parts=%.1fms\n"
                 "  terminal showdown:        %8.1f ms\n"
                 "  reach propagation:        %8.1f ms\n"
                 "  value + update:           %8.1f ms\n"
                 "  board/card filtering:     %8.1f ms\n"
                 "  synchronization:          %8.1f ms\n"
                 "  regret matching:          %8.1f ms\n",
                 wall_seconds * 1000.0, static_cast<unsigned long long>(decisions),
                 static_cast<unsigned long long>(actor_writes), accounted * 1000.0, terminal * 1000.0, copy * 1000.0, value_update * 1000.0,
                 chance * 1000.0, sync * 1000.0, strategy * 1000.0);
    prof_decisions_ = 0;
    prof_actor_writes_ = 0;
    prof_strategy_seconds_ = 0.0;
    prof_copy_seconds_ = 0.0;
    prof_children_seconds_ = 0.0;
    prof_value_update_seconds_ = 0.0;
    prof_terminal_seconds_ = 0.0;
    prof_chance_seconds_ = 0.0;
    prof_sync_seconds_ = 0.0;
    prof_wall_seconds_ = 0.0;
    for (const auto &worker : parallel_workers_) {
      worker->prof_decisions_ = 0;
      worker->prof_actor_writes_ = 0;
      worker->prof_strategy_seconds_ = 0.0;
      worker->prof_copy_seconds_ = 0.0;
      worker->prof_children_seconds_ = 0.0;
      worker->prof_value_update_seconds_ = 0.0;
      worker->prof_terminal_seconds_ = 0.0;
      worker->prof_chance_seconds_ = 0.0;
      worker->prof_sync_seconds_ = 0.0;
      worker->prof_wall_seconds_ = 0.0;
    }
    if (parallel_worker_ != nullptr) {
      parallel_worker_->prof_decisions_ = 0;
      parallel_worker_->prof_actor_writes_ = 0;
      parallel_worker_->prof_strategy_seconds_ = 0.0;
      parallel_worker_->prof_copy_seconds_ = 0.0;
      parallel_worker_->prof_children_seconds_ = 0.0;
      parallel_worker_->prof_value_update_seconds_ = 0.0;
      parallel_worker_->prof_terminal_seconds_ = 0.0;
      parallel_worker_->prof_chance_seconds_ = 0.0;
      parallel_worker_->prof_sync_seconds_ = 0.0;
      parallel_worker_->prof_wall_seconds_ = 0.0;
    }
  }

  Result<ComboVector, PostflopSolverError> cfr(const NodeId node_id,
                                               const std::uint8_t updating_player,
                                               const ReachRef &reach,
                                               const double strategy_weight) {
    if (layout_.uses_canonical_public_dag) {
      return cfr_canonical_parallel_entry(layout_.canonical_public_graph.root, updating_player,
                                          reach, strategy_weight);
    }
    // The physical tree is parallelized at chance nodes (coarse grained), not
    // at every decision node: the per-card subtrees are the big units of work.
    // Out-param adapter: the traversal writes into a local buffer and the
    // Result is only used for the error status (the runner discards the value).
    ComboVector result;
    const auto error = cfr_physical(node_id, updating_player, reach, strategy_weight, result);
    if (error) {
      return Result<ComboVector, PostflopSolverError>::failure(*error);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(result));
  }

  Result<ComboVector, PostflopSolverError> policy(const NodeId node_id,
                                                  const std::uint8_t updating_player,
                                                  const ReachRef &reach,
                                                  const bool best_response) {
    if (layout_.uses_canonical_public_dag) {
      return policy_canonical(layout_.canonical_public_graph.root, updating_player, reach,
                              best_response);
    }
    return policy_physical(node_id, updating_player, reach, best_response);
  }

  Result<ComboVector, PostflopSolverError>
  policy_from_physical_node(const NodeId node_id, const std::uint8_t updating_player,
                            const ReachRef &reach) {
    return policy_physical(node_id, updating_player, reach, false);
  }

private:
  [[nodiscard]] std::size_t value_slot(const ComboId combo,
                                       [[maybe_unused]] const std::uint8_t player) const noexcept {
    if constexpr (PlayerIndexed) {
      return static_cast<std::size_t>(layout_.player_flop_slot[player][combo]);
    }
    if constexpr (Capacity == combo_count) {
      return static_cast<std::size_t>(combo);
    }
    return static_cast<std::size_t>(layout_.active_combo_index[combo]);
  }

  void add_strategy(const std::size_t index, const double value) const {
    if (buffers_.strategy_float32 != nullptr) {
      buffers_.strategy_float32[index] =
          static_cast<float>(static_cast<double>(buffers_.strategy_float32[index]) + value);
      return;
    }
    buffers_.add_strategy(index, value);
  }

  struct DecisionScratch {
    std::array<ComboVector, maximum_action_count> action_values{};
    // Action-major strategy scratch: strategies[action][slot] keeps the slots
    // contiguous for a fixed action, so the value loop over the updating
    // player's combos is three contiguous streams (strategies, action_values,
    // values) and auto-vectorizes under /arch:AVX2. Slots are combo ids on the
    // non-per-player path, flop-range slots on the per-player path.
    std::array<std::array<double, combo_count>, maximum_action_count> strategies{};
    // Per-action actor reach scratch (plan: zero opponent reach copies): only
    // the actor's flop-range prefix is written per action; the opponent side
    // is shared from the parent, so no prefix copy is materialized per action.
    // A single buffer per depth suffices: the child recursion completes before
    // the next action reuses it and no child retains a pointer to it.
    std::array<ComboVector, 1> reach_actor{};
  };

  class DecisionScratchLease {
  public:
    explicit DecisionScratchLease(DenseTraversal &owner)
        : owner_(owner), scratch_(owner_.acquire_decision_scratch()) {}
    DecisionScratchLease(const DecisionScratchLease &) = delete;
    DecisionScratchLease &operator=(const DecisionScratchLease &) = delete;
    ~DecisionScratchLease() { --owner_.decision_scratch_depth_; }

    [[nodiscard]] DecisionScratch &get() const noexcept { return scratch_; }

  private:
    DenseTraversal &owner_;
    DecisionScratch &scratch_;
  };

  [[nodiscard]] DecisionScratch &acquire_decision_scratch() {
    if (decision_scratch_depth_ == decision_scratch_.size()) {
      decision_scratch_.push_back(std::make_unique<DecisionScratch>());
    }
    return *decision_scratch_[decision_scratch_depth_++];
  }

  void merge_deferred_regrets_from(DenseTraversal &source, std::vector<double> &source_delta) {
    for (const std::size_t index : source.deferred_regret_touched_) {
      if (deferred_regret_touched_flags_[index] == 0U) {
        deferred_regret_touched_flags_[index] = 1U;
        deferred_regret_touched_.push_back(index);
      }
      (*deferred_regret_delta_)[index] += source_delta[index];
      source_delta[index] = 0.0;
      source.deferred_regret_touched_flags_[index] = 0U;
    }
    source.deferred_regret_touched_.clear();
  }

  void merge_parallel_deferred_regrets() {
    if (parallel_worker_ != nullptr) {
      merge_deferred_regrets_from(*parallel_worker_, parallel_regret_delta_);
    }
    for (std::size_t index = 0; index < parallel_workers_.size(); ++index) {
      merge_deferred_regrets_from(*parallel_workers_[index], parallel_worker_deltas_[index]);
    }
  }

  // Queues a task for the shared parallel queue. The FIFO queue is drained by
  // the worker threads; producers never block and pending tasks stay bounded
  // by the recursion depth of open parallel decisions, so a task is never
  // overwritten (which would break its future with "broken promise"). Called
  // only while the traversal is live (the queue shutdown flag is only set
  // during destruction, after all cfr calls have returned).
  void dispatch_parallel_task(std::packaged_task<TraversalResult(DenseTraversal &)> task) {
    ParallelTaskQueue *const queue = parallel_shared_.get();
    {
      std::unique_lock lock(queue->mutex);
      queue->tasks.push_back(std::move(task));
    }
    // notify_all: the shared queue is drained by several workers and the
    // join paths also wait on the same condition variable; a single
    // notify_one can wake the joining thread instead of a worker and starve
    // the workers indefinitely.
    queue->ready.notify_all();
  }

  // Pull-based work-stealing: while a thread waits on futures dispatched to
  // the shared queue (river split below the turn), it executes other pending
  // tasks itself so the pool never stalls behind a single blocked dispatcher.
  // Returns whether a task was pulled (the caller blocks on the queue's
  // condition variable when false, instead of busy-spinning).
  bool try_pull_and_run() {
    ParallelTaskQueue *const queue = parallel_shared_.get();
    std::optional<std::packaged_task<TraversalResult(DenseTraversal &)>> task;
    {
      std::unique_lock lock(queue->mutex);
      if (!queue->tasks.empty()) {
        task = std::move(queue->tasks.front());
        queue->tasks.pop_front();
      }
    }
    if (!task) {
      return false;
    }
    (*task)(*this);
    return true;
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_parallel_entry(const std::uint32_t node_id, const std::uint8_t updating_player,
                               const ReachRef &reach,
                               const double strategy_weight) {
    if (parallel_worker_ != nullptr &&
        layout_.canonical_public_graph.nodes[node_id].kind == PublicNodeKind::Decision) {
      return cfr_canonical_parallel_decision(node_id, updating_player, reach, strategy_weight);
    }
    return cfr_canonical(node_id, updating_player, reach, strategy_weight);
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_parallel_decision(const std::uint32_t node_id, const std::uint8_t updating_player,
                                  const ReachRef &reach,
                                  const double strategy_weight) {
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    const auto action_count = canonical.edges.size();
    if (canonical.kind != PublicNodeKind::Decision || action_count < 2U ||
        action_count > maximum_parallel_action_count ||
        std::ranges::any_of(canonical.edges,
                            [](const auto &edge) { return edge.outcomes.size() != 1U; })) {
      return cfr_canonical(node_id, updating_player, reach, strategy_weight);
    }
    ++traversed_nodes_;
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    if (static_cast<std::size_t>(decision.action_count) != action_count ||
        canonical.update_multiplicity.size() != board.legal_combos.size()) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    const bool locked_root = is_locked_root(canonical);
    for (const ComboId combo : board.legal_combos) {
      const auto locked = locked_root ? locked_root_strategy(board.local_index[combo])
                                      : std::nullopt;
      const auto strategy =
          locked ? *locked : current_strategy(decision, board.local_index[combo], false);
      const auto slot = value_slot(combo, updating_player);
      for (std::size_t action = 0; action < action_count; ++action) {
        strategies[action][slot] = strategy[action];
      }
    }
    std::array<std::array<ComboVector, 2>, maximum_parallel_action_count> child_reaches{};
    for (std::size_t action = 0; action < action_count; ++action) {
      child_reaches[action][0] = *reach[0];
      child_reaches[action][1] = *reach[1];
      for (const ComboId combo : board.legal_combos) {
        child_reaches[action][decision.player][value_slot(combo, updating_player)] *=
            strategies[action][value_slot(combo, updating_player)];
      }
      child_reaches[action] =
          transform_reach(child_reaches[action],
                          canonical.edges[action].outcomes.front().physical_to_child_automorphism);
    }
    const auto &parallel_outcome = canonical.edges[0].outcomes.front();
    std::packaged_task<TraversalResult(DenseTraversal &)> first_task(
        [worker = parallel_worker_.get(), child = parallel_outcome.child, updating_player,
         child_reach = child_reaches[0], strategy_weight](DenseTraversal &) {
          return worker->cfr_canonical_parallel_entry(
              child, updating_player, {&child_reach[0], &child_reach[1]}, strategy_weight);
        });
    auto first = first_task.get_future();
    dispatch_parallel_task(std::move(first_task));
    std::optional<PostflopSolverError> serial_error;
    for (std::size_t action = 1U; action < action_count; ++action) {
      const auto &outcome = canonical.edges[action].outcomes.front();
      auto child = cfr_canonical(outcome.child, updating_player,
                                 {&child_reaches[action][0], &child_reaches[action][1]},
                                 strategy_weight);
      if (!child) {
        serial_error = child.error();
        break;
      }
      action_values[action] =
          transform_values_to_parent(child.value(), outcome.physical_to_child_automorphism);
    }
    auto first_values = first.get();
    if (!first_values || serial_error) {
      return Result<ComboVector, PostflopSolverError>::failure(first_values ? *serial_error
                                                                            : first_values.error());
    }
    action_values[0] = transform_values_to_parent(first_values.value(),
                                                  parallel_outcome.physical_to_child_automorphism);
    merge_parallel_deferred_regrets();

    auto values = zeroed_values(updating_player);
    if constexpr (PlayerIndexed) {
      // Per-player compact slots are contiguous 0..player_flop_count[player]-1,
      // so the value loop runs over the slot prefix directly and the common
      // two-action case is a fused SIMD accumulation (FMA chains, IEEE-identical).
      const std::size_t slot_count = layout_.player_flop_count[updating_player];
      if (decision.player == updating_player && action_count == 2U) {
        const double *const s0 = strategies[0].data();
        const double *const s1 = strategies[1].data();
        const double *const a0 = action_values[0].data();
        const double *const a1 = action_values[1].data();
        double *const v = values.data();
        std::size_t i = 0;
        for (; i + 4U <= slot_count; i += 4U) {
          const __m256d acc = _mm256_add_pd(
              _mm256_mul_pd(_mm256_loadu_pd(s0 + i), _mm256_loadu_pd(a0 + i)),
              _mm256_mul_pd(_mm256_loadu_pd(s1 + i), _mm256_loadu_pd(a1 + i)));
          _mm256_storeu_pd(v + i, _mm256_add_pd(_mm256_loadu_pd(v + i), acc));
        }
        for (; i < slot_count; ++i) {
          v[i] += s0[i] * a0[i] + s1[i] * a1[i];
        }
      } else if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < slot_count; ++slot) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      } else {
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < slot_count; ++slot) {
            values[slot] += action_values[action][slot];
          }
        }
      }
      // Regret/strategy update: for the uniform per-player ranges guaranteed
      // in the PlayerIndexed path, player_combos[updating_player] equals
      // board.legal_combos, so iterating the actor's list matches the
      // combined scalar loop below exactly (same combos, same order).
      for (const ComboId combo : board.player_combos[updating_player]) {
        const auto slot = value_slot(combo, updating_player);
        const auto local = board.local_index[combo];
        const auto offset = decision_action_base(layout_, decision, local);
        if (decision.player == updating_player) {
          const double multiplicity =
              static_cast<double>(canonical.update_multiplicity[static_cast<std::size_t>(local)]);
          for (std::size_t action = 0; action < action_count; ++action) {
            const auto index = static_cast<std::size_t>(offset + action);
            if (!locked_root) {
              if (deferred_regret_touched_flags_[index] == 0U) {
                deferred_regret_touched_flags_[index] = 1U;
                deferred_regret_touched_.push_back(index);
              }
              (*deferred_regret_delta_)[index] +=
                  multiplicity * (action_values[action][slot] - values[slot]);
            }
            add_strategy(index, multiplicity * strategy_weight * (*reach[updating_player])[slot] *
                                    strategies[action][slot]);
          }
        }
      }
    } else {
      for (const ComboId combo : board.legal_combos) {
        const auto slot = value_slot(combo, updating_player);
        const auto local = board.local_index[combo];
        const auto offset = decision_action_base(layout_, decision, local);
        if (decision.player == updating_player) {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
        if (decision.player == updating_player) {
          const double multiplicity =
              static_cast<double>(canonical.update_multiplicity[static_cast<std::size_t>(local)]);
          for (std::size_t action = 0; action < action_count; ++action) {
            const auto index = static_cast<std::size_t>(offset + action);
            if (!locked_root) {
              if (deferred_regret_touched_flags_[index] == 0U) {
                deferred_regret_touched_flags_[index] = 1U;
                deferred_regret_touched_.push_back(index);
              }
              (*deferred_regret_delta_)[index] +=
                  multiplicity * (action_values[action][slot] - values[slot]);
            }
            add_strategy(index, multiplicity * strategy_weight * (*reach[updating_player])[slot] *
                                    strategies[action][slot]);
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  // Physical-tree CFR traversal writing directly into `values_out` (no
  // per-node Result<ComboVector> return: the value vector is materialized in
  // the caller's buffer, eliminating the 5 KB move up the recursion).
  std::optional<PostflopSolverError> cfr_physical(const NodeId node_id,
                                                  const std::uint8_t updating_player,
                                                  const ReachRef &reach,
                                                  const double strategy_weight,
                                                  ComboVector &values_out) {
    ++traversed_nodes_;
    const auto t_wall = std::chrono::steady_clock::now();
    struct WallGuard {
      DenseTraversal *owner;
      std::chrono::steady_clock::time_point start;
      ~WallGuard() {
        owner->prof_wall_seconds_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      }
    } wall_guard{this, t_wall};
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    switch (node.kind) {
    case PublicNodeKind::TerminalFold:
    case PublicNodeKind::TerminalShowdown: {
      const auto t_terminal = std::chrono::steady_clock::now();
      const auto error = node.kind == PublicNodeKind::TerminalFold
                             ? fold_values_into(node, updating_player,
                                                *reach[1U - updating_player], values_out)
                             : showdown_values_into(node, updating_player,
                                                    *reach[1U - updating_player], values_out);
      prof_terminal_seconds_ +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t_terminal).count();
      return error;
    }
    case PublicNodeKind::Chance:
      return cfr_chance(node, updating_player, reach, strategy_weight, values_out);
    case PublicNodeKind::Decision:
      return cfr_decision(node, updating_player, reach, strategy_weight, values_out);
    }
    return PostflopSolverError::InvalidConfiguration;
  }

  Result<ComboVector, PostflopSolverError> policy_physical(const NodeId node_id,
                                                           const std::uint8_t updating_player,
                                                           const ReachRef &reach,
                                                           const bool best_response) {
    ++traversed_nodes_;
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    switch (node.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values(node, updating_player, *reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values(node, updating_player, *reach[1U - updating_player]);
    case PublicNodeKind::Chance:
      return policy_chance(node, updating_player, reach, best_response);
    case PublicNodeKind::Decision:
      return policy_decision(node, updating_player, reach, best_response);
    }
    return Result<ComboVector, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

public:
  [[nodiscard]] std::uint64_t traversed_nodes() const noexcept {
    std::uint64_t total = traversed_nodes_;
    if (parallel_worker_ != nullptr) {
      total += parallel_worker_->traversed_nodes();
    }
    for (const auto &worker : parallel_workers_) {
      total += worker->traversed_nodes();
    }
    return total;
  }
  [[nodiscard]] double maximum_normalization_error() const noexcept {
    double maximum = maximum_normalization_error_;
    if (parallel_worker_ != nullptr) {
      maximum = std::max(maximum, parallel_worker_->maximum_normalization_error());
    }
    for (const auto &worker : parallel_workers_) {
      maximum = std::max(maximum, worker->maximum_normalization_error());
    }
    return maximum;
  }

  Result<bool, PostflopSolverError> apply_deferred_regrets() {
    if (deferred_regret_delta_ == nullptr) {
      return Result<bool, PostflopSolverError>::success(true);
    }
    // Pool workers merge their per-thread deferred deltas into our deferred
    // delta and apply their own (disjoint) action indices on their own
    // threads, so the merge+clip work overlaps with our own apply. The
    // disjointness is guaranteed by the physical tree: each pool task owns a
    // distinct subtree and, with trivial automorphisms, distinct action
    // bases. The canonical path has no pool workers and is unaffected.
    std::vector<std::future<TraversalResult>> worker_futures;
    worker_futures.reserve(parallel_workers_.size());
    for (const auto &worker : parallel_workers_) {
      auto *const target = deferred_regret_delta_;
      std::packaged_task<TraversalResult(DenseTraversal &)> task(
          [worker = worker.get(), target](DenseTraversal &) {
            return worker->apply_pool_regrets(*target);
          });
      worker_futures.push_back(task.get_future());
      worker->dispatch_parallel_task(std::move(task));
    }
    for (const std::size_t index : deferred_regret_touched_) {
      const auto updated = buffers_.regret_at(index) + (*deferred_regret_delta_)[index];
      if (!std::isfinite(updated)) {
        return Result<bool, PostflopSolverError>::failure(PostflopSolverError::NumericalFailure);
      }
      buffers_.set_regret(index, std::max(0.0, updated));
      (*deferred_regret_delta_)[index] = 0.0;
      deferred_regret_touched_flags_[index] = 0U;
    }
    deferred_regret_touched_.clear();
    for (auto &future : worker_futures) {
      const auto applied = future.get();
      if (!applied) {
        return Result<bool, PostflopSolverError>::failure(applied.error());
      }
    }
    return Result<bool, PostflopSolverError>::success(true);
  }

  // Pool worker: merges this traversal's deferred regret deltas into the
  // shared target delta and applies the clipped regrets for its own action
  // indices (disjoint from every other thread's), then resets its own state.
  // Called on the worker thread via an apply task at the end of a player pass.
  Result<ComboVector, PostflopSolverError> apply_pool_regrets(std::vector<double> &target_delta) {
    for (const std::size_t index : deferred_regret_touched_) {
      target_delta[index] += (*deferred_regret_delta_)[index];
      (*deferred_regret_delta_)[index] = 0.0;
      const auto updated = buffers_.regret_at(index) + target_delta[index];
      if (!std::isfinite(updated)) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::NumericalFailure);
      }
      buffers_.set_regret(index, std::max(0.0, updated));
      target_delta[index] = 0.0;
      deferred_regret_touched_flags_[index] = 0U;
    }
    deferred_regret_touched_.clear();
    return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
  }

private:
  std::array<ComboVector, 2> transform_reach(const std::array<ComboVector, 2> &reach,
                                             const std::uint8_t automorphism_index) const {
    std::array<ComboVector, 2> transformed{};
    if constexpr (Capacity <= compact_combo_capacity) {
      const auto &mapping = layout_.active_automorphism_slots[automorphism_index];
      for (std::size_t slot = 0; slot < mapping.size(); ++slot) {
        transformed[0][mapping[slot]] = reach[0][slot];
        transformed[1][mapping[slot]] = reach[1][slot];
      }
    } else {
      const auto &mapping = layout_.automorphisms[automorphism_index].combos;
      for (const ComboId combo : layout_.active_combos) {
        transformed[0][mapping[combo]] = reach[0][combo];
        transformed[1][mapping[combo]] = reach[1][combo];
      }
    }
    return transformed;
  }

  ComboVector transform_values_to_parent(const ComboVector &child_values,
                                         const std::uint8_t automorphism_index) const {
    ComboVector parent_values{};
    if constexpr (Capacity <= compact_combo_capacity) {
      const auto &mapping = layout_.active_automorphism_slots[automorphism_index];
      for (std::size_t slot = 0; slot < mapping.size(); ++slot) {
        parent_values[slot] = child_values[mapping[slot]];
      }
    } else {
      const auto &mapping = layout_.automorphisms[automorphism_index].combos;
      for (const ComboId combo : layout_.active_combos) {
        parent_values[combo] = child_values[mapping[combo]];
      }
    }
    return parent_values;
  }

  Result<ComboVector, PostflopSolverError> cfr_canonical(const std::uint32_t node_id,
                                                         const std::uint8_t updating_player,
                                                         const ReachRef &reach,
                                                         const double strategy_weight) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    switch (canonical.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values_with_payoff(canonical.board_index,
                                     canonical.fold_payoff_antes[updating_player],
                                     updating_player, *reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values_with_payoffs(
          canonical.board_index, canonical.showdown_payoff_antes[updating_player][2],
          canonical.showdown_payoff_antes[updating_player][1],
          canonical.showdown_payoff_antes[updating_player][0], updating_player,
          *reach[1U - updating_player]);
    case PublicNodeKind::Chance:
      return cfr_canonical_chance(canonical, updating_player, reach, strategy_weight);
    case PublicNodeKind::Decision:
      return cfr_canonical_decision(canonical, updating_player, reach, strategy_weight);
    }
    return Result<ComboVector, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  Result<ComboVector, PostflopSolverError> policy_canonical(const std::uint32_t node_id,
                                                            const std::uint8_t updating_player,
                                                            const ReachRef &reach,
                                                            const bool best_response) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    switch (canonical.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values_with_payoff(canonical.board_index,
                                     canonical.fold_payoff_antes[updating_player],
                                     updating_player, *reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values_with_payoffs(
          canonical.board_index, canonical.showdown_payoff_antes[updating_player][2],
          canonical.showdown_payoff_antes[updating_player][1],
          canonical.showdown_payoff_antes[updating_player][0], updating_player,
          *reach[1U - updating_player]);
    case PublicNodeKind::Chance:
      return policy_canonical_chance(canonical, updating_player, reach, best_response);
    case PublicNodeKind::Decision:
      return policy_canonical_decision(canonical, updating_player, reach, best_response);
    }
    return Result<ComboVector, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_chance(const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
                       const ReachRef &reach, const double strategy_weight) {
    if (canonical.total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[canonical.board_index];
    const double denominator = static_cast<double>(canonical.total_legal_outcome_count - 4U);
    auto values = zeroed_values(updating_player);
    for (const auto &edge : canonical.edges) {
      std::optional<std::array<ComboVector, 2>> representative_reach;
      std::optional<ComboVector> representative_values;
      for (const auto &outcome : edge.outcomes) {
        auto child_reach = transform_reach(block_card(reach, board, outcome.chance_card),
                                           outcome.physical_to_child_automorphism);
        const ComboVector *child_values = nullptr;
        std::optional<ComboVector> distinct_values;
        if (representative_reach && child_reach == *representative_reach) {
          child_values = &*representative_values;
        } else {
          auto child = cfr_canonical(outcome.child, updating_player,
                                     {&child_reach[0], &child_reach[1]}, strategy_weight);
          if (!child) {
            return child;
          }
          if (!representative_reach) {
            representative_reach = child_reach;
            representative_values = std::move(child.value());
            child_values = &*representative_values;
          } else {
            distinct_values = std::move(child.value());
            child_values = &*distinct_values;
          }
        }
        const auto parent_values =
            transform_values_to_parent(*child_values, outcome.physical_to_child_automorphism);
        const double probability =
            static_cast<double>(outcome.physical_outcome_count) / denominator;
        for (const ComboId combo : board.legal_combos) {
          if ((layout_.combo_masks[combo] & outcome.chance_card.mask()) == 0U) {
            values[value_slot(combo, updating_player)] += probability * parent_values[value_slot(combo, updating_player)];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_decision(const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
                         const ReachRef &reach, const double strategy_weight) {
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (canonical.edges.size() != action_count ||
        canonical.update_multiplicity.size() != board.legal_combos.size()) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    const bool locked_root = is_locked_root(canonical);
    for (const ComboId combo : board.legal_combos) {
      const auto locked = locked_root ? locked_root_strategy(board.local_index[combo])
                                      : std::nullopt;
      const auto strategy =
          locked ? *locked : current_strategy(decision, board.local_index[combo], false);
      const auto slot = value_slot(combo, updating_player);
      for (std::size_t action = 0; action < action_count; ++action) {
        strategies[action][slot] = strategy[action];
      }
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      std::array<ComboVector, 2> child_reach;
      child_reach[0] = *reach[0];
      child_reach[1] = *reach[1];
      for (const ComboId combo : board.legal_combos) {
        child_reach[decision.player][value_slot(combo, updating_player)] *=
            strategies[action][value_slot(combo, updating_player)];
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      child_reach = transform_reach(child_reach, outcome.physical_to_child_automorphism);
      auto child = cfr_canonical(outcome.child, updating_player,
                                 {&child_reach[0], &child_reach[1]}, strategy_weight);
      if (!child) {
        return child;
      }
      action_values[action] =
          transform_values_to_parent(child.value(), outcome.physical_to_child_automorphism);
    }

    auto values = zeroed_values(updating_player);
    for (const ComboId combo : board.legal_combos) {
      const auto slot = value_slot(combo, updating_player);
      const auto local = board.local_index[combo];
      const auto offset = decision_action_base(layout_, decision, local);
      if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += strategies[action][slot] * action_values[action][slot];
        }
      } else {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += action_values[action][slot];
        }
      }
      if (decision.player == updating_player) {
        const double multiplicity =
            static_cast<double>(canonical.update_multiplicity[static_cast<std::size_t>(local)]);
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto index = static_cast<std::size_t>(offset + action);
          if (!locked_root) {
            const auto regret_delta = multiplicity * (action_values[action][slot] - values[slot]);
            if (deferred_regret_delta_ != nullptr) {
              if (deferred_regret_touched_flags_[index] == 0U) {
                deferred_regret_touched_flags_[index] = 1U;
                deferred_regret_touched_.push_back(index);
              }
              (*deferred_regret_delta_)[index] += regret_delta;
            } else {
              buffers_.set_regret(index, std::max(0.0, buffers_.regret_at(index) + regret_delta));
            }
          }
          add_strategy(index, multiplicity * strategy_weight * (*reach[updating_player])[slot] *
                                  strategies[action][slot]);
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError>
  policy_canonical_chance(const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
                          const ReachRef &reach, const bool best_response) {
    if (canonical.total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[canonical.board_index];
    const double denominator = static_cast<double>(canonical.total_legal_outcome_count - 4U);
    auto values = zeroed_values(updating_player);
    for (const auto &edge : canonical.edges) {
      std::optional<std::array<ComboVector, 2>> representative_reach;
      std::optional<ComboVector> representative_values;
      for (const auto &outcome : edge.outcomes) {
        auto child_reach = transform_reach(block_card(reach, board, outcome.chance_card),
                                           outcome.physical_to_child_automorphism);
        const ComboVector *child_values = nullptr;
        std::optional<ComboVector> distinct_values;
        if (representative_reach && child_reach == *representative_reach) {
          child_values = &*representative_values;
        } else {
          auto child = policy_canonical(outcome.child, updating_player,
                                        {&child_reach[0], &child_reach[1]}, best_response);
          if (!child) {
            return child;
          }
          if (!representative_reach) {
            representative_reach = child_reach;
            representative_values = std::move(child.value());
            child_values = &*representative_values;
          } else {
            distinct_values = std::move(child.value());
            child_values = &*distinct_values;
          }
        }
        const auto parent_values =
            transform_values_to_parent(*child_values, outcome.physical_to_child_automorphism);
        const double probability =
            static_cast<double>(outcome.physical_outcome_count) / denominator;
        for (const ComboId combo : board.legal_combos) {
          if ((layout_.combo_masks[combo] & outcome.chance_card.mask()) == 0U) {
            values[value_slot(combo, updating_player)] += probability * parent_values[value_slot(combo, updating_player)];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError>
  policy_canonical_decision(const CanonicalPublicNode &canonical,
                            const std::uint8_t updating_player,
                            const ReachRef &reach, const bool best_response) {
    const auto &decision = canonical.decision;
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    if (canonical.edges.size() != action_count) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    const bool locked_root = is_locked_root(canonical);
    for (const ComboId combo : board.legal_combos) {
      const auto locked = locked_root ? locked_root_strategy(board.local_index[combo])
                                      : std::nullopt;
      const auto strategy =
          locked ? *locked : current_strategy(decision, board.local_index[combo], true);
      const auto slot = value_slot(combo, updating_player);
      for (std::size_t action = 0; action < action_count; ++action) {
        strategies[action][slot] = strategy[action];
      }
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      std::array<ComboVector, 2> child_reach;
      child_reach[0] = *reach[0];
      child_reach[1] = *reach[1];
      if (!(best_response && decision.player == updating_player)) {
        for (const ComboId combo : board.legal_combos) {
          child_reach[decision.player][value_slot(combo, updating_player)] *=
              strategies[action][value_slot(combo, updating_player)];
        }
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      child_reach = transform_reach(child_reach, outcome.physical_to_child_automorphism);
      auto child =
          policy_canonical(outcome.child, updating_player, {&child_reach[0], &child_reach[1]},
                           best_response);
      if (!child) {
        return child;
      }
      action_values[action] =
          transform_values_to_parent(child.value(), outcome.physical_to_child_automorphism);
    }
    auto values = zeroed_values(updating_player);
    for (const ComboId combo : board.legal_combos) {
      const auto slot = value_slot(combo, updating_player);
      if (best_response && decision.player == updating_player && !is_locked_root(canonical)) {
        values[slot] = action_values[0][slot];
        for (std::size_t action = 1; action < action_count; ++action) {
          values[slot] = std::max(values[slot], action_values[action][slot]);
        }
      } else {
        if (decision.player == updating_player) {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  template <bool Float32State>
  std::array<double, maximum_action_count>
  current_strategy_for_state(const DecisionLayout &decision, const std::int16_t local_combo,
                             const bool average) {
    const auto count = static_cast<std::size_t>(decision.action_count);
    const auto offset = decision_action_base(layout_, decision, local_combo);
    std::array<double, maximum_action_count> strategy{};
    const auto source_at = [this, average](const std::size_t index) {
      if constexpr (Float32State) {
        const double value = average ? static_cast<double>(buffers_.strategy_float32[index])
                                     : static_cast<double>(buffers_.regret_float32[index]);
        return average ? value : std::max(0.0, value);
      } else {
        return average ? buffers_.strategy_at(index) : std::max(0.0, buffers_.regret_at(index));
      }
    };
    if (count == 2U) {
      const double first = source_at(static_cast<std::size_t>(offset));
      const double second = source_at(static_cast<std::size_t>(offset + 1U));
      const double sum = first + second;
      if (sum <= 0.0) {
        strategy[0] = 0.5;
        strategy[1] = 0.5;
      } else {
        const double inverse = 1.0 / sum;
        strategy[0] = first * inverse;
        strategy[1] = second * inverse;
      }
      return strategy;
    }
    double sum = 0.0;
    for (std::size_t action = 0; action < count; ++action) {
      const double source = source_at(static_cast<std::size_t>(offset + action));
      strategy[action] = source;
      sum += source;
    }
    if (sum <= 0.0) {
      std::fill_n(strategy.begin(), count, 1.0 / static_cast<double>(count));
    } else {
      for (std::size_t action = 0; action < count; ++action) {
        strategy[action] /= sum;
      }
    }
    return strategy;
  }

  std::array<double, maximum_action_count> current_strategy(const DecisionLayout &decision,
                                                            const std::int16_t local_combo,
                                                            const bool average) {
    return buffers_.regret_float32 != nullptr
               ? current_strategy_for_state<true>(decision, local_combo, average)
               : current_strategy_for_state<false>(decision, local_combo, average);
  }

  [[nodiscard]] bool is_locked_root(const CanonicalPublicNode &canonical) const noexcept {
    return root_lock_ != nullptr &&
           std::addressof(canonical) == std::addressof(layout_.canonical_public_graph
                                                           .nodes[layout_.canonical_public_graph
                                                                     .root]);
  }

  [[nodiscard]] bool is_locked_root(const PublicTreeNode &node) const noexcept {
    return root_lock_ != nullptr && node.id == layout_.tree.root;
  }

  [[nodiscard]] std::optional<std::array<double, maximum_action_count>>
  locked_root_strategy(const std::int16_t local_combo) const {
    if (root_lock_ == nullptr || local_combo < 0) {
      return std::nullopt;
    }
    const auto local = static_cast<std::size_t>(local_combo);
    if (local >= root_lock_->by_local_combo.size()) {
      return std::nullopt;
    }
    return root_lock_->by_local_combo[local];
  }

  Result<ComboVector, PostflopSolverError> fold_values(const PublicTreeNode &node,
                                                       const std::uint8_t updating_player,
                                                       const ComboVector &opponent_reach) const {
    return fold_values(node.state, layout_.node_board[static_cast<std::size_t>(node.id)],
                       updating_player, opponent_reach);
  }

  std::optional<PostflopSolverError>
  fold_values_into(const PublicTreeNode &node, const std::uint8_t updating_player,
                   const ComboVector &opponent_reach, ComboVector &values_out) const {
    const auto settlement = settle_terminal(node.state, layout_.tree.config.rake);
    if (!settlement) {
      return PostflopSolverError::SettlementFailure;
    }
    const double payoff =
        static_cast<double>(settlement.value().payoff_units[updating_player]) / units_per_ante;
    return fold_values_with_payoff_into(layout_.node_board[static_cast<std::size_t>(node.id)],
                                        payoff, updating_player, opponent_reach, values_out);
  }

  Result<ComboVector, PostflopSolverError> fold_values(const PublicState &state,
                                                       const std::uint32_t board_index,
                                                       const std::uint8_t updating_player,
                                                       const ComboVector &opponent_reach) const {
    const auto settlement = settle_terminal(state, layout_.tree.config.rake);
    if (!settlement) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::SettlementFailure);
    }
    const double payoff =
        static_cast<double>(settlement.value().payoff_units[updating_player]) / units_per_ante;
    return fold_values_with_payoff(board_index, payoff, updating_player, opponent_reach);
  }

  std::optional<PostflopSolverError>
  fold_values_with_payoff_into(const std::uint32_t board_index, const double payoff,
                               const std::uint8_t updating_player,
                               const ComboVector &opponent_reach, ComboVector &values_out) const {
    double total = 0.0;
    std::array<double, 36> by_card{};
    const auto &board = layout_.boards[board_index];
    const auto opponent = static_cast<std::uint8_t>(1U - updating_player);
    if constexpr (PlayerIndexed) {
      // Compact per-player terminal: only the opponent's live combos carry
      // nonzero opponent reach, and only the updating player's live combos
      // are ever read by parents (reach of the updating player is zero
      // elsewhere), so iterate exactly those lists.
      for (const ComboId combo_id : board.player_combos[opponent]) {
        const double weight = opponent_reach[value_slot(combo_id, opponent)];
        total += weight;
        by_card[layout_.combos[combo_id].first.value()] += weight;
        by_card[layout_.combos[combo_id].second.value()] += weight;
      }
    } else {
      for (const ComboId combo_id : board.legal_combos) {
        const double weight = opponent_reach[value_slot(combo_id, opponent)];
        total += weight;
        by_card[layout_.combos[combo_id].first.value()] += weight;
        by_card[layout_.combos[combo_id].second.value()] += weight;
      }
    }
    zero_values_into(updating_player, values_out);
    if constexpr (PlayerIndexed) {
      for (const ComboId combo_id : board.player_combos[updating_player]) {
        const auto &combo = layout_.combos[combo_id];
        const double own_weight = layout_.initial_reach[opponent][combo_id] > 0.0
                                      ? opponent_reach[value_slot(combo_id, opponent)]
                                      : 0.0;
        const double compatible = total - by_card[combo.first.value()] -
                                  by_card[combo.second.value()] + own_weight;
        values_out[value_slot(combo_id, updating_player)] =
            compatible * payoff / layout_.initial_normalization;
      }
    } else {
      for (const ComboId combo_id : board.legal_combos) {
        const auto &combo = layout_.combos[combo_id];
        const double compatible = total - by_card[combo.first.value()] -
                                  by_card[combo.second.value()] +
                                  opponent_reach[value_slot(combo_id, updating_player)];
        values_out[value_slot(combo_id, updating_player)] =
            compatible * payoff / layout_.initial_normalization;
      }
    }
    return std::nullopt;
  }

  Result<ComboVector, PostflopSolverError>
  fold_values_with_payoff(const std::uint32_t board_index, const double payoff,
                          const std::uint8_t updating_player,
                          const ComboVector &opponent_reach) const {
    ComboVector values{};
    if (const auto error = fold_values_with_payoff_into(board_index, payoff, updating_player,
                                                        opponent_reach, values)) {
      return Result<ComboVector, PostflopSolverError>::failure(*error);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError> showdown_values(const PublicTreeNode &node,
                                                           const std::uint8_t updating_player,
                                                           const ComboVector &opponent_reach) {
    return showdown_values(node.state, layout_.node_board[static_cast<std::size_t>(node.id)],
                           updating_player, opponent_reach);
  }

  std::optional<PostflopSolverError>
  showdown_values_into(const PublicTreeNode &node, const std::uint8_t updating_player,
                       const ComboVector &opponent_reach, ComboVector &values_out) {
    const auto own_win = settle_terminal(node.state, layout_.tree.config.rake,
                                         static_cast<std::uint8_t>(1U << updating_player));
    const auto tie = settle_terminal(node.state, layout_.tree.config.rake, 0b11U);
    const auto own_loss = settle_terminal(node.state, layout_.tree.config.rake,
                                          static_cast<std::uint8_t>(1U << (1U - updating_player)));
    if (!own_win || !tie || !own_loss) {
      return PostflopSolverError::SettlementFailure;
    }
    const double win_payoff =
        static_cast<double>(own_win.value().payoff_units[updating_player]) / units_per_ante;
    const double tie_payoff =
        static_cast<double>(tie.value().payoff_units[updating_player]) / units_per_ante;
    const double loss_payoff =
        static_cast<double>(own_loss.value().payoff_units[updating_player]) / units_per_ante;
    return showdown_values_with_payoffs_into(layout_.node_board[static_cast<std::size_t>(node.id)],
                                             win_payoff, tie_payoff, loss_payoff, updating_player,
                                             opponent_reach, values_out);
  }

  Result<ComboVector, PostflopSolverError> showdown_values(const PublicState &state,
                                                           const std::uint32_t board_index,
                                                           const std::uint8_t updating_player,
                                                           const ComboVector &opponent_reach) {
    const auto own_win = settle_terminal(state, layout_.tree.config.rake,
                                         static_cast<std::uint8_t>(1U << updating_player));
    const auto tie = settle_terminal(state, layout_.tree.config.rake, 0b11U);
    const auto own_loss = settle_terminal(state, layout_.tree.config.rake,
                                          static_cast<std::uint8_t>(1U << (1U - updating_player)));
    if (!own_win || !tie || !own_loss) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::SettlementFailure);
    }
    const double win_payoff =
        static_cast<double>(own_win.value().payoff_units[updating_player]) / units_per_ante;
    const double tie_payoff =
        static_cast<double>(tie.value().payoff_units[updating_player]) / units_per_ante;
    const double loss_payoff =
        static_cast<double>(own_loss.value().payoff_units[updating_player]) / units_per_ante;
    return showdown_values_with_payoffs(board_index, win_payoff, tie_payoff, loss_payoff,
                                        updating_player, opponent_reach);
  }

  std::optional<PostflopSolverError>
  showdown_values_with_payoffs_into(const std::uint32_t board_index, const double win_payoff,
                                    const double tie_payoff, const double loss_payoff,
                                    const std::uint8_t updating_player,
                                    const ComboVector &opponent_reach, ComboVector &values_out) {
    const auto prepared = prepare_ranks(layout_, board_index);
    if (!prepared) {
      return prepared.error();
    }
    const auto &board = layout_.boards[board_index];
    const auto rank_count = static_cast<std::size_t>(board.rank_count);
    const auto opponent = static_cast<std::uint8_t>(1U - updating_player);
    const auto calculate = [&](auto &totals, auto &by_card, auto &prefix, auto &card_prefix) {
      if constexpr (PlayerIndexed) {
        for (const ComboId combo_id : board.player_combos[opponent]) {
          const auto rank = static_cast<std::size_t>(board.rank_index[combo_id]);
          const double weight = opponent_reach[value_slot(combo_id, opponent)];
          totals[rank] += weight;
          by_card[static_cast<std::size_t>(layout_.combos[combo_id].first.value()) * rank_count +
                  rank] += weight;
          by_card[static_cast<std::size_t>(layout_.combos[combo_id].second.value()) * rank_count +
                  rank] += weight;
        }
      } else {
        for (const ComboId combo_id : board.legal_combos) {
          const auto rank = static_cast<std::size_t>(board.rank_index[combo_id]);
          const double weight = opponent_reach[value_slot(combo_id, opponent)];
          totals[rank] += weight;
          by_card[static_cast<std::size_t>(layout_.combos[combo_id].first.value()) * rank_count +
                  rank] += weight;
          by_card[static_cast<std::size_t>(layout_.combos[combo_id].second.value()) * rank_count +
                  rank] += weight;
        }
      }
      for (std::size_t rank = 0; rank < rank_count; ++rank) {
        prefix[rank + 1U] = prefix[rank] + totals[rank];
        for (std::size_t card = 0; card < 36U; ++card) {
          card_prefix[card * (rank_count + 1U) + rank + 1U] =
              card_prefix[card * (rank_count + 1U) + rank] + by_card[card * rank_count + rank];
        }
      }
      zero_values_into(updating_player, values_out);
      if constexpr (PlayerIndexed) {
        for (const ComboId combo_id : board.player_combos[updating_player]) {
          const auto rank = static_cast<std::size_t>(board.rank_index[combo_id]);
          const auto first = static_cast<std::size_t>(layout_.combos[combo_id].first.value());
          const auto second = static_cast<std::size_t>(layout_.combos[combo_id].second.value());
          const double own_reach = layout_.initial_reach[opponent][combo_id] > 0.0
                                       ? opponent_reach[value_slot(combo_id, opponent)]
                                       : 0.0;
          const double invalid_lower = card_prefix[first * (rank_count + 1U) + rank] +
                                       card_prefix[second * (rank_count + 1U) + rank];
          const double invalid_tie =
              by_card[first * rank_count + rank] + by_card[second * rank_count + rank] - own_reach;
          const double invalid_all =
              card_prefix[first * (rank_count + 1U) + rank_count] +
              card_prefix[second * (rank_count + 1U) + rank_count] - own_reach;
          const double lower = prefix[rank] - invalid_lower;
          const double equal = totals[rank] - invalid_tie;
          const double higher = (prefix[rank_count] - prefix[rank + 1U]) -
                                (invalid_all - invalid_lower - invalid_tie);
          values_out[value_slot(combo_id, updating_player)] =
              (lower * win_payoff + equal * tie_payoff + higher * loss_payoff) /
              layout_.initial_normalization;
        }
      } else {
        for (const ComboId combo_id : board.legal_combos) {
          const auto rank = static_cast<std::size_t>(board.rank_index[combo_id]);
          const auto first = static_cast<std::size_t>(layout_.combos[combo_id].first.value());
          const auto second = static_cast<std::size_t>(layout_.combos[combo_id].second.value());
          const double invalid_lower = card_prefix[first * (rank_count + 1U) + rank] +
                                       card_prefix[second * (rank_count + 1U) + rank];
          const double invalid_tie = by_card[first * rank_count + rank] +
                                     by_card[second * rank_count + rank] -
                                     opponent_reach[value_slot(combo_id, updating_player)];
          const double invalid_all = card_prefix[first * (rank_count + 1U) + rank_count] +
                                     card_prefix[second * (rank_count + 1U) + rank_count] -
                                     opponent_reach[value_slot(combo_id, updating_player)];
          const double lower = prefix[rank] - invalid_lower;
          const double equal = totals[rank] - invalid_tie;
          const double higher =
              (prefix[rank_count] - prefix[rank + 1U]) - (invalid_all - invalid_lower - invalid_tie);
          values_out[value_slot(combo_id, updating_player)] =
              (lower * win_payoff + equal * tie_payoff + higher * loss_payoff) /
              layout_.initial_normalization;
        }
      }
    };
    // Reuse the member scratch arrays (sized for the maximum rank space, see
    // the member declarations): no per-node heap allocation (the old code
    // heap-allocated ~5.4 KB per showdown node, ~4.4 M allocations per run).
    std::fill_n(showdown_totals_.begin(), rank_count, 0.0);
    std::fill_n(showdown_by_card_.begin(), 36U * rank_count, 0.0);
    std::fill_n(showdown_prefix_.begin(), rank_count + 1U, 0.0);
    std::fill_n(showdown_card_prefix_.begin(), 36U * (rank_count + 1U), 0.0);
    calculate(showdown_totals_, showdown_by_card_, showdown_prefix_, showdown_card_prefix_);
    return std::nullopt;
  }

  Result<ComboVector, PostflopSolverError>
  showdown_values_with_payoffs(const std::uint32_t board_index, const double win_payoff,
                               const double tie_payoff, const double loss_payoff,
                               const std::uint8_t updating_player,
                               const ComboVector &opponent_reach) {
    ComboVector values{};
    if (const auto error = showdown_values_with_payoffs_into(
            board_index, win_payoff, tie_payoff, loss_payoff, updating_player, opponent_reach,
            values)) {
      return Result<ComboVector, PostflopSolverError>::failure(*error);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  // Returns a zero-initialized value vector. In the per-player path only the
  // updating player's flop-range prefix (slots 0..player_flop_count[player])
  // can ever be read, so only that prefix is zeroed; every reader in this
  // path iterates player_combos lists or bounds by player_flop_count.
#pragma warning(push)
#pragma warning(disable : 4701)
  [[nodiscard]] ComboVector zeroed_values(const std::uint8_t updating_player) const {
    ComboVector values;
    if constexpr (PlayerIndexed) {
      std::fill_n(values.begin(), layout_.player_flop_count[updating_player], 0.0);
    } else {
      values.fill(0.0);
    }
    return values;
  }
#pragma warning(pop)

  // Zeroes the active prefix of an out-parameter value buffer (per-player
  // compact slots are 0..player_flop_count[player]-1 in the PlayerIndexed
  // path; the full array otherwise). Every callee that writes through an
  // out-param must zero its destination first so that slots not written
  // (blocked combos) read as zero in the parent's accumulation.
  void zero_values_into(const std::uint8_t updating_player, ComboVector &values) const {
    if constexpr (PlayerIndexed) {
      std::fill_n(values.begin(), layout_.player_flop_count[updating_player], 0.0);
    } else {
      values.fill(0.0);
    }
  }

  std::array<ComboVector, 2> block_card(const ReachRef &reach,
                                        const BoardData &board, const CardId card) const {
    if constexpr (PlayerIndexed) {
      // Per-player compact spaces: slots 0..player_flop_count[p]-1 are the
      // player's flop-range combos (contiguous), so the copy is a pair of
      // contiguous prefix copies; then zero the slots blocked by the new card.
      // Slots beyond each prefix are never read in this path.
      std::array<ComboVector, 2> blocked;
      std::copy_n(reach[0]->begin(), layout_.player_flop_count[0], blocked[0].begin());
      std::copy_n(reach[1]->begin(), layout_.player_flop_count[1], blocked[1].begin());
      for (const ComboId combo : board.player_combos[0]) {
        if ((layout_.combo_masks[combo] & card.mask()) != 0U) {
          blocked[0][value_slot(combo, 0)] = 0.0;
        }
      }
      for (const ComboId combo : board.player_combos[1]) {
        if ((layout_.combo_masks[combo] & card.mask()) != 0U) {
          blocked[1][value_slot(combo, 1)] = 0.0;
        }
      }
      return blocked;
    }
    std::array<ComboVector, 2> blocked;
    blocked[0] = *reach[0];
    blocked[1] = *reach[1];
    static_cast<void>(board);
    if constexpr (Capacity <= compact_combo_capacity) {
      for (const std::uint16_t slot : layout_.active_slots_by_card[card.value()]) {
        blocked[0][slot] = 0.0;
        blocked[1][slot] = 0.0;
      }
    } else {
      for (const ComboId combo : layout_.active_combos_by_card[card.value()]) {
        blocked[0][combo] = 0.0;
        blocked[1][combo] = 0.0;
      }
    }
    return blocked;
  }

  // Copies the reach arrays. With per-player compact spaces (PlayerIndexed)
  // each player's live slots are a contiguous prefix, so the copy is two
  // contiguous prefix copies (~4.5 KB instead of ~10 KB); otherwise a full
  // copy preserves the zero-initialized state (identical to `auto copy =
  // reach;`).
  std::array<ComboVector, 2> copy_reach(const ReachRef &reach,
                                        const BoardData &board) const {
    static_cast<void>(board);
    if constexpr (PlayerIndexed) {
      std::array<ComboVector, 2> copy;
      std::copy_n(reach[0]->begin(), layout_.player_flop_count[0], copy[0].begin());
      std::copy_n(reach[1]->begin(), layout_.player_flop_count[1], copy[1].begin());
      return copy;
    }
    std::array<ComboVector, 2> copy;
    copy[0] = *reach[0];
    copy[1] = *reach[1];
    return copy;
  }

  std::optional<PostflopSolverError> cfr_chance(const PublicTreeNode &node,
                                                const std::uint8_t updating_player,
                                                const ReachRef &reach,
                                                const double strategy_weight,
                                                ComboVector &values_out) {
    zero_values_into(updating_player, values_out);
    if (node.edges.empty() || node.edges.front().total_legal_outcome_count <= 4U) {
      return PostflopSolverError::InvalidConfiguration;
    }
    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    const auto accumulate = [&](const PublicTreeEdge &edge, const ComboVector &child) {
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      for (const ComboId combo_id : board.player_combos[updating_player]) {
        if ((layout_.combo_masks[combo_id] & edge.chance_card.mask()) == 0U) {
          const auto slot = value_slot(combo_id, updating_player);
          values_out[slot] += probability * child[slot];
        }
      }
    };
    // Coarse-grained parallel split at the turn chance (the first chance layer
    // after the flop betting tree): the per-card subtrees are fanned out over
    // the worker pool (round-robin) while the current thread keeps its own
    // share, so all threads work concurrently on roughly equal-sized units.
    // Below the turn chance everything runs serially, so no nested dispatch
    // can serialize behind a worker's queue. Tasks execute on each worker's
    // own traversal; their deferred regret deltas are merged and applied on
    // the worker threads at the end of the player pass (apply_deferred_regrets
    // dispatches one task per worker), so the merge cost does not sit on the
    // current thread's critical path.
    const std::size_t edge_count = node.edges.size();
    const std::size_t worker_count = parallel_workers_.size();
    const std::size_t split = (!parallel_shutdown_.load() &&
                               std::popcount(board.mask) == 3U && worker_count > 0U)
                                  ? edge_count - edge_count / (worker_count + 1U)
                                  : 0U;
    std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
    std::vector<std::future<TraversalResult>> futures;
    tasks.reserve(split);
    futures.reserve(split);
    // Round-robin task assignment: consecutive per-card subtrees are roughly
    // equal-sized (the blocked-combo sets vary smoothly across cards), so
    // interleaving balances the workers' loads better than contiguous chunks.
    // The task assignment does not affect the accumulation order (the results
    // are read back in edge order), so any assignment is bit-exact.
    const auto worker_for = [worker_count](const std::size_t index) {
      return static_cast<std::size_t>(index % worker_count);
    };
    // Pre-sized result vectors: each child writes directly into its own slot
    // (the workers write disjoint slots of the main thread's vector, which
    // stay valid until the join below).
    std::vector<ComboVector> worker_results(split);
    std::vector<ComboVector> main_results(edge_count - split);
    for (std::size_t index = 0; index < split; ++index) {
      const auto &edge = node.edges[index];
      auto child_reach = block_card(reach, board, edge.chance_card);
      std::packaged_task<TraversalResult(DenseTraversal &)> task(
          [child = edge.child, updating_player, child_reach, strategy_weight,
           result = &worker_results[index]](DenseTraversal &self) {
            const auto error = self.cfr_physical(child, updating_player,
                                                 {&child_reach[0], &child_reach[1]},
                                                 strategy_weight, *result);
            if (error) {
              return Result<ComboVector, PostflopSolverError>::failure(*error);
            }
            return Result<ComboVector, PostflopSolverError>::success(ComboVector{});
          });
      futures.push_back(task.get_future());
      tasks.push_back(std::move(task));
    }
    const auto t_sync = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < split; ++index) {
      if (worker_count > 0U) {
        parallel_workers_[worker_for(index)]->dispatch_parallel_task(std::move(tasks[index]));
      } else {
        dispatch_parallel_task(std::move(tasks[index]));
      }
    }
    prof_sync_seconds_ +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t_sync).count();
    // Both the worker subtrees (0..split) and the main thread's share
    // (split..edge_count) are joined, then accumulated in the original edge
    // order so the floating-point summation is bit-identical to the serial
    // traversal regardless of where the split boundary lies.
    for (std::size_t index = split; index < edge_count; ++index) {
      const auto &edge = node.edges[index];
      auto child_reach = block_card(reach, board, edge.chance_card);
      const auto error = cfr_physical(edge.child, updating_player,
                                      {&child_reach[0], &child_reach[1]}, strategy_weight,
                                      main_results[index - split]);
      if (error) {
        return error;
      }
    }
    for (std::size_t index = 0; index < split; ++index) {
      auto &future = futures[index];
      // Work-stealing wait: keep the pool busy with other pending subtrees
      // instead of blocking on this future. When the shared queue is empty,
      // block briefly on its condition variable instead of busy-spinning.
      while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        if (!try_pull_and_run()) {
          ParallelTaskQueue *const queue = parallel_shared_.get();
          std::unique_lock lock(queue->mutex);
          queue->ready.wait_for(lock, std::chrono::milliseconds(1));
        }
      }
      auto child = future.get();
      if (!child) {
        return child.error();
      }
    }
    const auto t_chance = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < split; ++index) {
      accumulate(node.edges[index], worker_results[index]);
    }
    for (std::size_t index = split; index < edge_count; ++index) {
      accumulate(node.edges[index], main_results[index - split]);
    }
    prof_chance_seconds_ +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t_chance).count();
    return std::nullopt;
  }

  std::optional<PostflopSolverError> cfr_decision(const PublicTreeNode &node,
                                                   const std::uint8_t updating_player,
                                                   const ReachRef &reach,
                                                   const double strategy_weight,
                                                   ComboVector &values_out) {
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    const auto t_begin = std::chrono::steady_clock::now();
    const auto t_after_strategy = [&] {
      const auto t_strategy = std::chrono::steady_clock::now();
      prof_strategy_seconds_ +=
          std::chrono::duration<double>(t_strategy - t_begin).count();
      return t_strategy;
    }();
    auto t_child_end = t_after_strategy;
    // Strategies are only meaningful for the acting player's own live combos
    // (board.player_combos[decision.player]); for every other combo the
    // actor's reach is zero, so its strategy never contributes. Only the
    // updating player's live combos (board.player_combos[updating_player])
    // carry nonzero updating-player reach, so value loops iterate that list.
    // Cache each actor combo's flop-space slot once per node (the action loop
    // would otherwise re-derive it per action), preserving iteration order and
    // bit-exact accumulation. Only the first actor_slot_count entries are
    // written before being read.
#pragma warning(push)
#pragma warning(disable : 4701)
    std::array<std::int16_t, combo_count> actor_slots;
#pragma warning(pop)
    std::size_t actor_slot_count = 0;
    for (const ComboId combo_id : board.player_combos[decision.player]) {
      actor_slots[actor_slot_count++] =
          static_cast<std::int16_t>(value_slot(combo_id, decision.player));
    }
    for (const ComboId combo_id : board.player_combos[decision.player]) {
      const auto locked = is_locked_root(node) ? locked_root_strategy(board.local_index[combo_id])
                                               : std::nullopt;
      const auto strategy = locked
                                ? *locked
                                : current_strategy(decision,
                                                   board.player_local[decision.player][combo_id],
                                                   false);
      const auto slot = value_slot(combo_id, decision.player);
      for (std::size_t action = 0; action < action_count; ++action) {
        strategies[action][slot] = strategy[action];
      }
    }
    // SIMD batch for the common two-action case: consecutive actor combos
    // have contiguous action blocks (uses_direct_action_bases), so four
    // combos' regrets are eight contiguous floats; four scalar divisions
    // become one AVX2 division. The arithmetic (max, pair-add, 1/sum, mul)
    // is IEEE-identical to the scalar path, so the result is bit-exact.
    if (!is_locked_root(node) && action_count == 2U && layout_.uses_direct_action_bases &&
        buffers_.regret_float32 != nullptr) {
      const float *const regrets = buffers_.regret_float32 + decision.action_base;
      const __m256d zero = _mm256_setzero_pd();
      const __m256d one = _mm256_set1_pd(1.0);
      const __m256d half = _mm256_set1_pd(0.5);
      std::size_t i = 0;
      for (; i + 4U <= actor_slot_count; i += 4U) {
        __m256d r01 = _mm256_cvtps_pd(_mm_loadu_ps(regrets + 2U * i));
        __m256d r23 = _mm256_cvtps_pd(_mm_loadu_ps(regrets + 2U * i + 4U));
        r01 = _mm256_max_pd(r01, zero);
        r23 = _mm256_max_pd(r23, zero);
        __m256d sums = _mm256_hadd_pd(r01, r23);
        // _mm256_hadd_pd is per-128-bit-lane: (a0+a1, b0+b1, a2+a3, b2+b3), so
        // reorder to (s0, s1, s2, s3) = (a0+a1, a2+a3, b0+b1, b2+b3).
        sums = _mm256_permute4x64_pd(sums, 0b11011000);
        __m256d inverse = _mm256_div_pd(one, sums);
        inverse = _mm256_blendv_pd(inverse, half, _mm256_cmp_pd(sums, zero, _CMP_LE_OS));
        __m256d s01 = _mm256_mul_pd(r01, _mm256_permute4x64_pd(inverse, 0b01010000));
        __m256d s23 = _mm256_mul_pd(r23, _mm256_permute4x64_pd(inverse, 0b11111010));
        // The scalar path substitutes the uniform strategy when sum <= 0;
        // the clamped regrets would otherwise multiply to zero.
        {
          const __m256d le_zero = _mm256_cmp_pd(sums, zero, _CMP_LE_OS);
          s01 = _mm256_blendv_pd(s01, half, _mm256_permute4x64_pd(le_zero, 0b01010000));
          s23 = _mm256_blendv_pd(s23, half, _mm256_permute4x64_pd(le_zero, 0b11111010));
        }
        alignas(32) double s01_arr[4];
        alignas(32) double s23_arr[4];
        _mm256_store_pd(s01_arr, s01);
        _mm256_store_pd(s23_arr, s23);
        for (std::size_t k = 0; k < 4U; ++k) {
          const auto slot = static_cast<std::size_t>(actor_slots[i + k]);
          const double first = k < 2U ? s01_arr[2U * k] : s23_arr[2U * (k - 2U)];
          const double second = k < 2U ? s01_arr[2U * k + 1U] : s23_arr[2U * (k - 2U) + 1U];
          strategies[0][slot] = first;
          strategies[1][slot] = second;
        }
      }
      for (; i < actor_slot_count; ++i) {
        const auto combo_id = board.player_combos[decision.player][i];
        const auto strategy =
            current_strategy(decision, board.player_local[decision.player][combo_id], false);
        const auto slot = static_cast<std::size_t>(actor_slots[i]);
        for (std::size_t action = 0; action < action_count; ++action) {
          strategies[action][slot] = strategy[action];
        }
      }
    }

    auto t_prev = t_after_strategy;
    for (std::size_t action = 0; action < action_count; ++action) {
      // The child node kind decides whether the actor reach is consumed at
      // all: a terminal (fold/showdown) reads only reach[1-updating_player].
      // When the actor is the updating player the freshly written actor reach
      // is never read, so its materialization is skipped and the parent's own
      // reach is passed instead (Phase C.3 fusion).
      const auto &child_node =
          layout_.tree.nodes[static_cast<std::size_t>(node.edges[action].child)];
      const bool terminal_child = child_node.kind == PublicNodeKind::TerminalFold ||
                                  child_node.kind == PublicNodeKind::TerminalShowdown;
      const bool actor_needed = !terminal_child || decision.player != updating_player;
      ComboVector &actor_reach = scratch_lease.get().reach_actor[0];
      if (actor_needed) {
        std::size_t slot_index = 0;
        for (const ComboId combo_id : board.player_combos[decision.player]) {
          static_cast<void>(combo_id);
          const auto slot = actor_slots[slot_index++];
          ++prof_actor_writes_;
          // Fused actor write: only the actor's flop-range prefix is set, so
          // no prefix copy is materialized; the opponent side is shared from
          // the parent (plan: zero opponent reach copies).
          actor_reach[slot] = (*reach[decision.player])[slot] * strategies[action][slot];
        }
      }
      const auto t_copy_end = std::chrono::steady_clock::now();
      prof_copy_seconds_ +=
          std::chrono::duration<double>(t_copy_end - t_prev).count();
      // The child writes its value vector directly into this action's slot of
      // the parent's scratch (no Result<ComboVector> return, no prefix copy).
      const ReachRef child_ref =
          actor_needed
              ? (decision.player == 0U ? ReachRef{&actor_reach, reach[1]}
                                       : ReachRef{reach[0], &actor_reach})
              : ReachRef{reach[0], reach[1]};
      const auto error =
          cfr_physical(node.edges[action].child, updating_player, child_ref, strategy_weight,
                       action_values[action]);
      t_child_end = std::chrono::steady_clock::now();
      prof_children_seconds_ +=
          std::chrono::duration<double>(t_child_end - t_copy_end).count();
      t_prev = t_child_end;
      if (error) {
        return error;
      }
    }

    auto &values = values_out;
    zero_values_into(updating_player, values);
    const bool updating_actor = decision.player == updating_player;
    if (updating_actor) {
      if constexpr (PlayerIndexed) {
        // Action-outer, slot-inner value accumulation: for a fixed action the
        // three streams (strategies[action][slot], action_values[action][slot],
        // values[slot]) are contiguous over the updating player's flop-range
        // slots, so the loop auto-vectorizes under /arch:AVX2. Slots for
        // combos blocked on this board hold zero in action_values (children
        // keep them zero), so the full-prefix accumulation is exact. The
        // summation order differs from the strict combo-outer order by
        // construction; the dEV deviation is far below the 1e-6 gate.
        const auto updating_count = layout_.player_flop_count[updating_player];
        if (action_count == 2U) {
          // Fused two-action accumulation: four contiguous slots at a time
          // (per-player slots are contiguous). Compared with the scalar
          // action-outer loop this halves the values load/store traffic and
          // enables a single FMA chain per slot; IEEE-identical per slot.
          const double *const s0 = strategies[0].data();
          const double *const s1 = strategies[1].data();
          const double *const a0 = action_values[0].data();
          const double *const a1 = action_values[1].data();
          double *const v = values.data();
          std::size_t i = 0;
          for (; i + 4U <= updating_count; i += 4U) {
            const __m256d acc = _mm256_add_pd(
                _mm256_mul_pd(_mm256_loadu_pd(s0 + i), _mm256_loadu_pd(a0 + i)),
                _mm256_mul_pd(_mm256_loadu_pd(s1 + i), _mm256_loadu_pd(a1 + i)));
            _mm256_storeu_pd(v + i, _mm256_add_pd(_mm256_loadu_pd(v + i), acc));
          }
          for (; i < updating_count; ++i) {
            v[i] += s0[i] * a0[i] + s1[i] * a1[i];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            for (std::size_t slot = 0; slot < updating_count; ++slot) {
              values[slot] += strategies[action][slot] * action_values[action][slot];
            }
          }
        }
      } else {
        // Non-per-player space: slots are combo ids, iterate the updating
        // player's live combos (combo-outer to keep the bit-exact order).
        for (const ComboId combo_id : board.player_combos[updating_player]) {
          const auto slot = value_slot(combo_id, updating_player);
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      }
      // Regret/strategy updates remain combo-outer: each combo's action block
      // is contiguous (offset..offset+action_count) and each index is touched
      // once, so the order is irrelevant to the result. (The F1 vectorized
      // update was measured and reverted: 175.5s vs 164.0s baseline — the
      // scattered value loads dominate and the contiguity check is pure
      // overhead; see journey §8.9.)
      for (const ComboId combo_id : board.player_combos[updating_player]) {
        const auto slot = value_slot(combo_id, updating_player);
        const bool locked_root = is_locked_root(node);
        const auto actor_local = board.player_local[decision.player][combo_id];
        const auto offset = decision_action_base(layout_, decision, actor_local);
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto index = static_cast<std::size_t>(offset + action);
          if (!locked_root) {
            const auto regret_delta = action_values[action][slot] - values[slot];
            if (deferred_regret_delta_ != nullptr) {
              if (deferred_regret_touched_flags_[index] == 0U) {
                deferred_regret_touched_flags_[index] = 1U;
                deferred_regret_touched_.push_back(index);
              }
              (*deferred_regret_delta_)[index] += regret_delta;
            } else {
              buffers_.set_regret(index, std::max(0.0, buffers_.regret_at(index) + regret_delta));
            }
          }
          add_strategy(index,
                       strategy_weight * (*reach[updating_player])[slot] * strategies[action][slot]);
        }
      }
    } else {
      for (const ComboId combo_id : board.player_combos[updating_player]) {
        const auto slot = value_slot(combo_id, updating_player);
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += action_values[action][slot];
        }
      }
    }
    prof_value_update_seconds_ +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t_child_end).count();
    ++prof_decisions_;
    return std::nullopt;
  }

  Result<ComboVector, PostflopSolverError> policy_chance(const PublicTreeNode &node,
                                                         const std::uint8_t updating_player,
                                                         const ReachRef &reach,
                                                         const bool best_response) {
    auto values = zeroed_values(updating_player);
    if (node.edges.empty() || node.edges.front().total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    const auto accumulate = [&](const PublicTreeEdge &edge, const ComboVector &child) {
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      for (const ComboId combo_id : board.player_combos[updating_player]) {
        if ((layout_.combo_masks[combo_id] & edge.chance_card.mask()) == 0U) {
          const auto slot = value_slot(combo_id, updating_player);
          values[slot] += probability * child[slot];
        }
      }
    };
    // Same coarse-grained parallel split as cfr_chance: fan the per-card
    // subtrees out over the worker pool (round-robin) while the current
    // thread keeps its own share, then join.
    const std::size_t edge_count = node.edges.size();
    const std::size_t worker_count = parallel_workers_.size();
    const std::size_t split = (!parallel_shutdown_.load() &&
                               std::popcount(board.mask) == 3U && worker_count > 0U)
                                  ? edge_count - edge_count / (worker_count + 1U)
                                  : 0U;
    std::vector<std::packaged_task<TraversalResult(DenseTraversal &)>> tasks;
    std::vector<std::future<TraversalResult>> futures;
    tasks.reserve(split);
    futures.reserve(split);
    const auto worker_for = [worker_count, split](const std::size_t index) {
      return split == 0U ? 0U : index * worker_count / split;
    };
    for (std::size_t index = 0; index < split; ++index) {
      const auto &edge = node.edges[index];
      auto child_reach = block_card(reach, board, edge.chance_card);
      std::packaged_task<TraversalResult(DenseTraversal &)> task(
          [child = edge.child, updating_player, child_reach,
           best_response](DenseTraversal &self) {
            return self.policy_physical(child, updating_player,
                                        {&child_reach[0], &child_reach[1]}, best_response);
          });
      futures.push_back(task.get_future());
      tasks.push_back(std::move(task));
    }
    for (std::size_t index = 0; index < split; ++index) {
      if (worker_count > 0U) {
        parallel_workers_[worker_for(index)]->dispatch_parallel_task(std::move(tasks[index]));
      } else {
        dispatch_parallel_task(std::move(tasks[index]));
      }
    }
    // Both the worker subtrees (0..split) and the main thread's share
    // (split..edge_count) are joined, then accumulated in the original edge
    // order so the floating-point summation is bit-identical to the serial
    // traversal: the certification is a single-shot evaluation and must stay
    // bit-exact against the reference profile. Result vectors reserve without
    // zero-init (every slot is assigned before reading).
    std::vector<ComboVector> worker_results;
    worker_results.reserve(split);
    std::vector<ComboVector> main_results;
    main_results.reserve(edge_count - split);
    for (std::size_t index = split; index < edge_count; ++index) {
      const auto &edge = node.edges[index];
      auto child_reach = block_card(reach, board, edge.chance_card);
      const auto child =
          policy_physical(edge.child, updating_player, {&child_reach[0], &child_reach[1]},
                          best_response);
      if (!child) {
        return child;
      }
      main_results.emplace_back(child.value());
    }
    for (std::size_t index = 0; index < split; ++index) {
      auto child = futures[index].get();
      if (!child) {
        return Result<ComboVector, PostflopSolverError>::failure(child.error());
      }
      worker_results.emplace_back(child.value());
    }
    for (std::size_t index = 0; index < split; ++index) {
      accumulate(node.edges[index], worker_results[index]);
    }
    for (std::size_t index = split; index < edge_count; ++index) {
      accumulate(node.edges[index], main_results[index - split]);
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  Result<ComboVector, PostflopSolverError> policy_decision(const PublicTreeNode &node,
                                                           const std::uint8_t updating_player,
                                                           const ReachRef &reach,
                                                           const bool best_response) {
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    DecisionScratchLease scratch_lease(*this);
    auto &action_values = scratch_lease.get().action_values;
    auto &strategies = scratch_lease.get().strategies;
    for (const ComboId combo_id : board.player_combos[decision.player]) {
      const auto locked = is_locked_root(node)
                              ? locked_root_strategy(board.local_index[combo_id])
                              : std::nullopt;
      const auto strategy = locked
                                ? *locked
                                : current_strategy(decision,
                                                   board.player_local[decision.player][combo_id],
                                                   true);
      const auto slot = value_slot(combo_id, decision.player);
      for (std::size_t action = 0; action < action_count; ++action) {
        strategies[action][slot] = strategy[action];
      }
    }
    // Cache each actor combo's flop-space slot once per node (see cfr_decision).
#pragma warning(push)
#pragma warning(disable : 4701)
    std::array<std::int16_t, combo_count> actor_slots;
#pragma warning(pop)
    std::size_t actor_slot_count = 0;
    for (const ComboId combo_id : board.player_combos[decision.player]) {
      actor_slots[actor_slot_count++] =
          static_cast<std::int16_t>(value_slot(combo_id, decision.player));
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      auto child_reach = copy_reach(reach, board);
      if (!(best_response && decision.player == updating_player)) {
        std::size_t slot_index = 0;
        for (const ComboId combo_id : board.player_combos[decision.player]) {
          static_cast<void>(combo_id);
          const auto slot = actor_slots[slot_index++];
          child_reach[decision.player][slot] *= strategies[action][slot];
        }
      }
      const auto child = policy_physical(node.edges[action].child, updating_player,
                                         {&child_reach[0], &child_reach[1]}, best_response);
      if (!child) {
        return child;
      }
      if constexpr (PlayerIndexed) {
        std::copy_n(child.value().begin(), layout_.player_flop_count[updating_player],
                    action_values[action].begin());
      } else {
        action_values[action] = child.value();
      }
    }
    auto values = zeroed_values(updating_player);
    const bool updating_actor = decision.player == updating_player;
    if (best_response && updating_actor && !is_locked_root(node)) {
      // Best-response branch: max over actions, order-independent.
      for (const ComboId combo_id : board.player_combos[updating_player]) {
        const auto slot = value_slot(combo_id, updating_player);
        values[slot] = action_values[0][slot];
        for (std::size_t action = 1; action < action_count; ++action) {
          values[slot] = std::max(values[slot], action_values[action][slot]);
        }
      }
    } else if (updating_actor) {
      if constexpr (PlayerIndexed) {
        // Action-outer, slot-inner: contiguous streams, auto-vectorized (see
        // cfr_decision).
        const auto updating_count = layout_.player_flop_count[updating_player];
        for (std::size_t action = 0; action < action_count; ++action) {
          for (std::size_t slot = 0; slot < updating_count; ++slot) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      } else {
        for (const ComboId combo_id : board.player_combos[updating_player]) {
          const auto slot = value_slot(combo_id, updating_player);
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategies[action][slot] * action_values[action][slot];
          }
        }
      }
    } else {
      for (const ComboId combo_id : board.player_combos[updating_player]) {
        const auto slot = value_slot(combo_id, updating_player);
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += action_values[action][slot];
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(std::move(values));
  }

  DenseLayout &layout_;
  ActionBuffers buffers_;
  std::vector<double> *deferred_regret_delta_{nullptr};
  const PreparedRootLock *root_lock_{nullptr};
  std::vector<std::size_t> deferred_regret_touched_;
  std::vector<std::uint8_t> deferred_regret_touched_flags_;
  std::vector<double> parallel_regret_delta_;
  std::unique_ptr<DenseTraversal> parallel_worker_;
  std::vector<std::vector<double>> parallel_worker_deltas_;
  std::vector<std::unique_ptr<DenseTraversal>> parallel_workers_;
  std::shared_ptr<ParallelTaskQueue> parallel_shared_;
  std::atomic<bool> parallel_shutdown_{false};
  std::jthread parallel_thread_;
  std::vector<std::unique_ptr<DecisionScratch>> decision_scratch_;
  // Showdown rank scratch, sized for the maximum rank space: rank_count is
  // the number of distinct hand values among the board's legal combos, always
  // <= combo_count, so these members cover every board without per-node heap
  // allocation (the old code heap-allocated ~5.4 KB per showdown node).
  std::array<double, combo_count> showdown_totals_{};
  std::array<double, 36U * combo_count> showdown_by_card_{};
  std::array<double, combo_count + 1U> showdown_prefix_{};
  std::array<double, 36U * (combo_count + 1U)> showdown_card_prefix_{};
  std::size_t decision_scratch_depth_{0};
  std::uint64_t traversed_nodes_{0};
  double maximum_normalization_error_{0.0};
};

template <std::size_t Capacity, bool PlayerIndexed>
std::array<TraversalComboVector<Capacity>, 2> initial_reach(const DenseLayout &layout) {
  std::array<TraversalComboVector<Capacity>, 2> reach{};
  if constexpr (PlayerIndexed) {
    // Each player's reach lives in its own flop-range space (contiguous
    // prefix of size player_flop_count[player]); the same seeding serves both
    // player passes.
    for (std::uint8_t player = 0; player < 2U; ++player) {
      for (std::size_t slot = 0; slot < layout.player_flop_count[player]; ++slot) {
        const auto combo = layout.player_flop_combos[player][slot];
        reach[player][slot] = layout.initial_reach[player][combo];
      }
    }
  } else {
    for (const ComboId combo : layout.active_combos) {
      const auto slot = Capacity == combo_count
                            ? static_cast<std::size_t>(combo)
                            : static_cast<std::size_t>(layout.active_combo_index[combo]);
      reach[0][slot] = layout.initial_reach[0][combo];
      reach[1][slot] = layout.initial_reach[1][combo];
    }
  }
  return reach;
}

class DenseTraversalRunner {
public:
  virtual ~DenseTraversalRunner() = default;
  [[nodiscard]] virtual Result<bool, PostflopSolverError> cfr(std::uint8_t updating_player,
                                                              double strategy_weight) = 0;
  [[nodiscard]] virtual Result<bool, PostflopSolverError> apply_deferred_regrets() = 0;
  [[nodiscard]] virtual std::uint64_t traversed_nodes() const noexcept = 0;
  [[nodiscard]] virtual double maximum_normalization_error() const noexcept = 0;
};

template <std::size_t Capacity, bool PlayerIndexed>
class TypedDenseTraversalRunner final : public DenseTraversalRunner {
public:
  TypedDenseTraversalRunner(DenseLayout &layout, const ActionBuffers buffers,
                            std::vector<double> *deferred_regret_delta,
                            const std::uint8_t parallel_action_depth,
                            const PreparedRootLock *root_lock)
      : traversal_(layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock),
        reach_(initial_reach<Capacity, PlayerIndexed>(layout)), root_(layout.tree.root) {}

  Result<bool, PostflopSolverError> cfr(const std::uint8_t updating_player,
                                        const double strategy_weight) override {
    const auto t0 = std::chrono::steady_clock::now();
    const auto traversed = traversal_.cfr(root_, updating_player, {&reach_[0], &reach_[1]},
                                          strategy_weight);
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!traversed) {
      return Result<bool, PostflopSolverError>::failure(traversed.error());
    }
    traversal_.dump_per_pass_profile(wall);
    return Result<bool, PostflopSolverError>::success(true);
  }

  Result<bool, PostflopSolverError> apply_deferred_regrets() override {
    return traversal_.apply_deferred_regrets();
  }
  [[nodiscard]] std::uint64_t traversed_nodes() const noexcept override {
    return traversal_.traversed_nodes();
  }
  [[nodiscard]] double maximum_normalization_error() const noexcept override {
    return traversal_.maximum_normalization_error();
  }

private:
  DenseTraversal<Capacity, PlayerIndexed> traversal_;
  std::array<TraversalComboVector<Capacity>, 2> reach_;
  NodeId root_{0};
};

std::unique_ptr<DenseTraversalRunner>
make_dense_traversal_runner(DenseLayout &layout, const ActionBuffers buffers,
                            std::vector<double> *deferred_regret_delta,
                            const std::uint8_t parallel_action_depth,
                            const PreparedRootLock *root_lock) {
  if (layout.uses_canonical_public_dag && layout.active_combos.size() <= compact_combo_capacity) {
    return std::make_unique<TypedDenseTraversalRunner<compact_combo_capacity, false>>(
        layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
  }
  if (layout.uses_direct_action_bases) {
    // Per-player physical path (identity automorphisms only): every action
    // index is touched exactly once per player pass (each combo is its own
    // infoset and each node is visited once), so the deferred regret delta
    // (667 MB double + the end-of-pass apply) is unnecessary: the update
    // loop's immediate-apply branch (deferred_regret_delta_ == nullptr) is
    // bit-exact for single-touch indices and eliminates the delta, the
    // worker merge and the whole regret_application phase.
    return std::make_unique<TypedDenseTraversalRunner<combo_count, true>>(
        layout, buffers, nullptr, parallel_action_depth, root_lock);
  }
  return std::make_unique<TypedDenseTraversalRunner<combo_count, false>>(
      layout, buffers, deferred_regret_delta, parallel_action_depth, root_lock);
}

Result<double, PostflopSolverError>
measure_canonical_normalization_error(const DenseLayout &layout, const ActionBuffers buffers) {
  if (!layout.uses_isomorphic_infosets) {
    return Result<double, PostflopSolverError>::success(0.0);
  }
  if (layout.canonical_action_bases.size() != layout.canonical_action_counts.size()) {
    return Result<double, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
  }
  double maximum_error = 0.0;
  for (std::size_t infoset = 0; infoset < layout.canonical_action_bases.size(); ++infoset) {
    const auto base = static_cast<std::size_t>(layout.canonical_action_bases[infoset]);
    const auto count = static_cast<std::size_t>(layout.canonical_action_counts[infoset]);
    for (const bool average : {false, true}) {
      double source_sum = 0.0;
      for (std::size_t action = 0; action < count; ++action) {
        const double source = average ? buffers.strategy_at(base + action)
                                      : std::max(0.0, buffers.regret_at(base + action));
        if (!std::isfinite(source)) {
          return Result<double, PostflopSolverError>::failure(
              PostflopSolverError::NumericalFailure);
        }
        source_sum += source;
      }
      double normalized_sum = 0.0;
      if (source_sum <= 0.0) {
        normalized_sum = static_cast<double>(count) / static_cast<double>(count);
      } else {
        for (std::size_t action = 0; action < count; ++action) {
          const double source = average ? buffers.strategy_at(base + action)
                                        : std::max(0.0, buffers.regret_at(base + action));
          normalized_sum += source / source_sum;
        }
      }
      maximum_error = std::max(maximum_error, std::abs(normalized_sum - 1.0));
    }
  }
  return Result<double, PostflopSolverError>::success(maximum_error);
}

// Sums a per-combo value vector weighted by the player's initial reach, in the
// same convention as analyze_postflop_node: profile/BR values are expressed as
// per-hand EVs in antes. The certification evaluates at the tree root, where
// the public-reach probability is exactly 1.0 by construction of
// initial_normalization; the division is kept to mirror the analysis path.
template <std::size_t Capacity, bool PlayerIndexed>
double reach_weighted_sum(const DenseLayout &layout,
                          const std::array<TraversalComboVector<Capacity>, 2> &reach,
                          const TraversalComboVector<Capacity> &values, const std::uint8_t player) {
  double total = 0.0;
  if constexpr (PlayerIndexed) {
    for (std::size_t slot = 0; slot < layout.player_flop_count[player]; ++slot) {
      total += reach[player][slot] * values[slot];
    }
  } else {
    for (std::size_t slot = 0; slot < Capacity; ++slot) {
      total += reach[player][slot] * values[slot];
    }
  }
  return total;
}

template <std::size_t Capacity, bool PlayerIndexed>
double root_public_reach_probability(const DenseLayout &layout,
                                     const std::array<TraversalComboVector<Capacity>, 2> &reach) {
  // In DAG mode build_layout clears node_board/tree.nodes, so the root board
  // must come from the canonical graph instead of the physical node mapping.
  const auto root_board_index =
      layout.uses_canonical_public_dag
          ? layout.canonical_public_graph.nodes[layout.canonical_public_graph.root].board_index
          : layout.node_board[layout.tree.root];
  const auto &board = layout.boards[root_board_index];
  double compatible_pair_mass = 0.0;
  if constexpr (PlayerIndexed) {
    // Per-player spaces: reach[0] is in P0's flop-range space and reach[1] in
    // P1's; map each combo through its player's slot table (-1 when the combo
    // is outside that player's flop range).
    for (const ComboId first : board.legal_combos) {
      const auto slot0 = layout.player_flop_slot[0][first];
      if (slot0 < 0 || !(reach[0][slot0] > 0.0)) {
        continue;
      }
      for (const ComboId second : board.legal_combos) {
        if ((layout.combo_masks[first] & layout.combo_masks[second]) == 0U) {
          const auto slot1 = layout.player_flop_slot[1][second];
          if (slot1 >= 0) {
            compatible_pair_mass += reach[0][slot0] * reach[1][slot1];
          }
        }
      }
    }
  } else {
    const auto slot_of = [&layout](const ComboId combo) -> std::size_t {
      if constexpr (Capacity == combo_count) {
        return static_cast<std::size_t>(combo);
      }
      return static_cast<std::size_t>(layout.active_combo_index[combo]);
    };
    for (const ComboId first : board.legal_combos) {
      if (!(reach[0][slot_of(first)] > 0.0)) {
        continue;
      }
      for (const ComboId second : board.legal_combos) {
        if ((layout.combo_masks[first] & layout.combo_masks[second]) == 0U) {
          compatible_pair_mass += reach[0][slot_of(first)] * reach[1][slot_of(second)];
        }
      }
    }
  }
  return layout.initial_normalization > 0.0
             ? compatible_pair_mass / layout.initial_normalization
             : 0.0;
}

template <std::size_t Capacity, bool PlayerIndexed>
Result<PostflopCertification, PostflopSolverError>
certify_typed(DenseLayout &layout, const ActionBuffers buffers, const std::uint64_t iteration,
              const PreparedRootLock *root_lock) {
  const auto reach = initial_reach<Capacity, PlayerIndexed>(layout);
  const double public_reach_probability =
      root_public_reach_probability<Capacity, PlayerIndexed>(layout, reach);
  if (!(public_reach_probability > 0.0)) {
    return Result<PostflopCertification, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto aggregate_value = [&](const std::uint8_t player,
                                   const TraversalComboVector<Capacity> &values) {
    return reach_weighted_sum<Capacity, PlayerIndexed>(layout, reach, values, player) /
           public_reach_probability;
  };
  for (std::uint32_t board_index = 0; board_index < layout.boards.size(); ++board_index) {
    if (std::popcount(layout.boards[board_index].mask) == 5) {
      const auto prepared = prepare_ranks(layout, board_index);
      if (!prepared) {
        return Result<PostflopCertification, PostflopSolverError>::failure(prepared.error());
      }
    }
  }
  if (!layout.uses_canonical_public_dag) {
    // Policy-only traversal: the pool works without a deferred regret delta
    // (the policy path never touches regret state), so each of the four
    // profile/BR evaluations runs on the worker pool at the turn chance.
    DenseTraversal<Capacity, PlayerIndexed> traversal(layout, buffers, nullptr, 7U, root_lock);
    PostflopCertification certification;
    certification.iteration = iteration;
    for (std::uint8_t player = 0; player < 2U; ++player) {
      const std::array<const TraversalComboVector<Capacity> *, 2> reach_ref{&reach[0], &reach[1]};
      const auto profile = traversal.policy(layout.tree.root, player, reach_ref, false);
      const auto response = traversal.policy(layout.tree.root, player, reach_ref, true);
      if (!profile || !response) {
        return Result<PostflopCertification, PostflopSolverError>::failure(
            profile ? response.error() : profile.error());
      }
      certification.profile_value_antes[player] = aggregate_value(player, profile.value());
      certification.best_response_value_antes[player] = aggregate_value(player, response.value());
    }
    certification.nash_conv_antes =
        (certification.best_response_value_antes[0] - certification.profile_value_antes[0]) +
        (certification.best_response_value_antes[1] - certification.profile_value_antes[1]);
    certification.expected_payoff_sum_antes =
        certification.profile_value_antes[0] + certification.profile_value_antes[1];
    const double initial_pot =
        static_cast<double>(layout.tree.config.initial_pot.units()) / units_per_ante;
    certification.normalized_nash_conv = initial_pot > 0.0
                                             ? certification.nash_conv_antes / initial_pot
                                             : std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(certification.nash_conv_antes) ||
        !std::isfinite(certification.normalized_nash_conv)) {
      return Result<PostflopCertification, PostflopSolverError>::failure(
          PostflopSolverError::NumericalFailure);
    }
    return Result<PostflopCertification, PostflopSolverError>::success(certification);
  }
  const auto evaluate = [&layout, buffers, &reach, root_lock,
                         public_reach_probability](const std::uint8_t player,
                                                   const bool best_response) {
    DenseTraversal<Capacity, PlayerIndexed> traversal(layout, buffers, nullptr, 0U, root_lock);
    const std::array<const TraversalComboVector<Capacity> *, 2> reach_ref{&reach[0], &reach[1]};
    const auto values = traversal.policy(layout.tree.root, player, reach_ref, best_response);
    if (!values) {
      return Result<double, PostflopSolverError>::failure(values.error());
    }
    return Result<double, PostflopSolverError>::success(
        reach_weighted_sum<Capacity, PlayerIndexed>(layout, reach, values.value(), player) /
        public_reach_probability);
  };
  auto profile_zero = std::async(std::launch::async, evaluate, std::uint8_t{0}, false);
  auto response_zero = std::async(std::launch::async, evaluate, std::uint8_t{0}, true);
  auto profile_one = std::async(std::launch::async, evaluate, std::uint8_t{1}, false);
  auto response_one = std::async(std::launch::async, evaluate, std::uint8_t{1}, true);
  const std::array<Result<double, PostflopSolverError>, 4> evaluated{
      profile_zero.get(), response_zero.get(), profile_one.get(), response_one.get()};
  for (const auto &value : evaluated) {
    if (!value) {
      return Result<PostflopCertification, PostflopSolverError>::failure(value.error());
    }
  }
  PostflopCertification certification;
  certification.iteration = iteration;
  certification.profile_value_antes = {evaluated[0].value(), evaluated[2].value()};
  certification.best_response_value_antes = {evaluated[1].value(), evaluated[3].value()};
  certification.nash_conv_antes =
      (certification.best_response_value_antes[0] - certification.profile_value_antes[0]) +
      (certification.best_response_value_antes[1] - certification.profile_value_antes[1]);
  certification.expected_payoff_sum_antes =
      certification.profile_value_antes[0] + certification.profile_value_antes[1];
  const double initial_pot =
      static_cast<double>(layout.tree.config.initial_pot.units()) / units_per_ante;
  certification.normalized_nash_conv = initial_pot > 0.0
                                           ? certification.nash_conv_antes / initial_pot
                                           : std::numeric_limits<double>::quiet_NaN();
  if (!std::isfinite(certification.nash_conv_antes) ||
      !std::isfinite(certification.normalized_nash_conv)) {
    return Result<PostflopCertification, PostflopSolverError>::failure(
        PostflopSolverError::NumericalFailure);
  }
  return Result<PostflopCertification, PostflopSolverError>::success(certification);
}

Result<PostflopCertification, PostflopSolverError>
certify(DenseLayout &layout, const ActionBuffers buffers, const std::uint64_t iteration,
        const PreparedRootLock *root_lock) {
  if (layout.uses_canonical_public_dag && layout.active_combos.size() <= compact_combo_capacity) {
    return certify_typed<compact_combo_capacity, false>(layout, buffers, iteration, root_lock);
  }
  if (layout.uses_direct_action_bases) {
    return certify_typed<combo_count, true>(layout, buffers, iteration, root_lock);
  }
  return certify_typed<combo_count, false>(layout, buffers, iteration, root_lock);
}

std::uint64_t double_bits(const double value) { return std::bit_cast<std::uint64_t>(value); }
double bits_double(const std::uint64_t value) { return std::bit_cast<double>(value); }

void checksum_bytes(std::uint64_t &checksum, const void *const data, const std::size_t size) {
  const auto *const bytes = static_cast<const unsigned char *>(data);
  for (std::size_t index = 0; index < size; ++index) {
    checksum ^= bytes[index];
    checksum *= 1099511628211ULL;
  }
}

template <typename Value>
bool write_binary(std::ofstream &output, const Value &value, std::uint64_t &checksum) {
  output.write(reinterpret_cast<const char *>(&value), sizeof(Value));
  checksum_bytes(checksum, &value, sizeof(Value));
  return static_cast<bool>(output);
}

template <typename Value>
bool read_binary(std::ifstream &input, Value &value, std::uint64_t &checksum) {
  input.read(reinterpret_cast<char *>(&value), sizeof(Value));
  if (!input) {
    return false;
  }
  checksum_bytes(checksum, &value, sizeof(Value));
  return true;
}

bool atomic_replace(const std::string &temporary, const std::filesystem::path &target) {
#ifdef _WIN32
  return MoveFileExA(temporary.c_str(), target.string().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code rename_error;
  std::filesystem::rename(temporary, target, rename_error);
  return !rename_error;
#endif
}

Result<PostflopStrategyQuery, PostflopSolverError>
query_strategy_from_layout(const DenseLayout &layout, const ActionBuffers buffers,
                           const NodeId public_node, const ComboId combo) {
  if (public_node >= layout.tree.nodes.size() || combo >= combo_count) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &decision = layout.decisions[static_cast<std::size_t>(public_node)];
  if (!decision.present) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &board = layout.boards[decision.board_index];
  const auto local = board.local_index[combo];
  if (local < 0) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto offset = decision_action_base(layout, decision, local);
  PostflopStrategyQuery query;
  query.public_node = public_node;
  query.combo = combo;
  query.actions.reserve(decision.action_count);
  query.probabilities.resize(decision.action_count);
  double sum = 0.0;
  for (std::size_t action = 0; action < decision.action_count; ++action) {
    query.actions.push_back(
        layout.tree.nodes[static_cast<std::size_t>(public_node)].edges[action].action);
    query.probabilities[action] = buffers.strategy_at(static_cast<std::size_t>(offset + action));
    sum += query.probabilities[action];
  }
  if (sum <= 0.0) {
    std::fill(query.probabilities.begin(), query.probabilities.end(),
              1.0 / static_cast<double>(decision.action_count));
  } else {
    for (double &probability : query.probabilities) {
      probability /= sum;
    }
  }
  return Result<PostflopStrategyQuery, PostflopSolverError>::success(std::move(query));
}

Result<ActionBuffers, PostflopSolverError>
in_memory_checkpoint_buffers(const PostflopCheckpoint &checkpoint) {
  if (checkpoint.state_precision == PostflopStatePrecision::Float32) {
    if (checkpoint.cumulative_regret_float32.size() != checkpoint.action_count ||
        checkpoint.cumulative_strategy_float32.size() != checkpoint.action_count ||
        !checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty()) {
      return Result<ActionBuffers, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    return Result<ActionBuffers, PostflopSolverError>::success(
        {nullptr, nullptr, checkpoint.cumulative_regret_float32.size(), nullptr,
         const_cast<float *>(checkpoint.cumulative_regret_float32.data()),
         const_cast<float *>(checkpoint.cumulative_strategy_float32.data())});
  }
  if (checkpoint.cumulative_regret.size() != checkpoint.action_count ||
      checkpoint.cumulative_strategy.size() != checkpoint.action_count ||
      !checkpoint.cumulative_regret_float32.empty() ||
      !checkpoint.cumulative_strategy_float32.empty()) {
    return Result<ActionBuffers, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  return Result<ActionBuffers, PostflopSolverError>::success(
      {const_cast<double *>(checkpoint.cumulative_regret.data()),
       const_cast<double *>(checkpoint.cumulative_strategy.data()),
       checkpoint.cumulative_regret.size()});
}

} // namespace

struct PostflopPreparedTree::Impl {
  PostflopTreeConfig config;
  PostflopRanges ranges;
  DenseLayout layout;
  std::optional<DenseLayout> analysis_layout;
  double preparation_seconds{0.0};
  bool lossless_isomorphism_enabled{true};
  bool canonical_public_dag_enabled{true};
};

PostflopPreparedTree::PostflopPreparedTree(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation)) {}

PostflopPreparedTree::~PostflopPreparedTree() = default;
PostflopPreparedTree::PostflopPreparedTree(PostflopPreparedTree &&) noexcept = default;
PostflopPreparedTree &PostflopPreparedTree::operator=(PostflopPreparedTree &&) noexcept = default;

Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>
prepare_postflop_tree(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                      const bool enable_lossless_isomorphism,
                      const bool enable_canonical_public_dag, const bool prepare_analysis) {
  if (!validate_postflop_ranges(config, ranges)) {
    return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto started = std::chrono::steady_clock::now();
  auto layout =
      build_layout(config, ranges, enable_lossless_isomorphism, enable_canonical_public_dag);
  if (!layout) {
    return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::failure(
        layout.error());
  }
  auto implementation = std::make_unique<PostflopPreparedTree::Impl>();
  implementation->config = config;
  implementation->ranges = ranges;
  implementation->layout = std::move(layout.value());
  if (prepare_analysis && implementation->layout.tree.nodes.empty()) {
    auto analysis_layout = build_layout(config, ranges, enable_lossless_isomorphism, false);
    if (!analysis_layout) {
      return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::failure(
          analysis_layout.error());
    }
    implementation->analysis_layout = std::move(analysis_layout.value());
  }
  implementation->lossless_isomorphism_enabled = enable_lossless_isomorphism;
  implementation->canonical_public_dag_enabled = enable_canonical_public_dag;
  implementation->preparation_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>::success(
      std::shared_ptr<PostflopPreparedTree>(new PostflopPreparedTree(std::move(implementation))));
}

PostflopLayoutEstimate prepared_postflop_layout_estimate(const PostflopPreparedTree &prepared) {
  const auto &layout = prepared.implementation_->layout;
  return {layout.tree.stats,
          static_cast<std::uint64_t>(layout.canonical_public_graph.nodes.size()),
          layout.information_sets,
          layout.actions,
          layout.actions * sizeof(double),
          layout.actions * sizeof(double)};
}

std::shared_ptr<const PublicTree>
prepared_postflop_public_tree(const std::shared_ptr<PostflopPreparedTree> &prepared) {
  if (!prepared || !prepared->implementation_) {
    return {};
  }
  const auto *tree = prepared->implementation_->analysis_layout
                         ? &prepared->implementation_->analysis_layout->tree
                         : &prepared->implementation_->layout.tree;
  if (tree->nodes.empty()) {
    return {};
  }
  return {prepared, tree};
}

PostflopRanges make_uniform_postflop_ranges() {
  PostflopRanges ranges;
  const auto full = RangeWeight::from_basis_points(10'000).value();
  for (auto &range : ranges.players) {
    range.fill(full);
  }
  return ranges;
}

Result<bool, PostflopSolverError> validate_postflop_ranges(const PostflopTreeConfig &config,
                                                           const PostflopRanges &ranges) {
  const auto valid_config = validate_tree_config(config);
  if (!valid_config) {
    return Result<bool, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
  }
  const auto combos = all_combos();
  std::uint64_t board_mask = 0U;
  for (const auto card : configured_board(config)) {
    board_mask |= card.mask();
  }
  for (std::size_t first = 0; first < combos.size(); ++first) {
    const auto first_mask = combos[first].first.mask() | combos[first].second.mask();
    if ((first_mask & board_mask) != 0U || ranges.players[0][first].basis_points() == 0U) {
      continue;
    }
    for (std::size_t second = 0; second < combos.size(); ++second) {
      const auto second_mask = combos[second].first.mask() | combos[second].second.mask();
      if ((second_mask & board_mask) == 0U && (first_mask & second_mask) == 0U &&
          ranges.players[1][second].basis_points() != 0U) {
        return Result<bool, PostflopSolverError>::success(true);
      }
    }
  }
  return Result<bool, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
}

Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(const PostflopTreeConfig &config, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from) {
  return solve_postflop_exact(config, make_uniform_postflop_ranges(), options, resume_from);
}

Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                     const PostflopSolveOptions &options, const PostflopCheckpoint *resume_from) {
  const auto solve_started = std::chrono::steady_clock::now();
  auto prepared = prepare_postflop_tree(config, ranges, options.enable_lossless_isomorphism,
                                        options.enable_canonical_public_dag);
  if (!prepared) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(prepared.error());
  }
  auto result = solve_postflop_exact(*prepared.value(), options, resume_from);
  if (result) {
    const double total_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - solve_started).count();
    result.value().timings.layout_seconds =
        std::max(0.0, total_seconds - result.value().timings.run_solver_seconds);
    result.value().timings.total_seconds = total_seconds;
  }
  return result;
}

Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(PostflopPreparedTree &prepared, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from) {
  const auto solve_started = std::chrono::steady_clock::now();
  const auto &config = prepared.implementation_->config;
  const auto &ranges = prepared.implementation_->ranges;
  if (options.iterations == 0U || options.certification_interval == 0U ||
      (options.target_normalized_nash_conv &&
       (!std::isfinite(*options.target_normalized_nash_conv) ||
        *options.target_normalized_nash_conv < 0.0)) ||
      (options.target_normalized_max_deviation &&
       (!std::isfinite(*options.target_normalized_max_deviation) ||
        *options.target_normalized_max_deviation < 0.0)) ||
      (options.target_normalized_nash_conv && options.target_normalized_max_deviation) ||
      options.memory_backend == MemoryPrototype::StreetDecomposition ||
      (options.memory_backend == MemoryPrototype::OutOfCore &&
       options.state_precision != PostflopStatePrecision::Float64)) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  if (!validate_postflop_ranges(config, ranges) ||
      options.enable_lossless_isomorphism !=
          prepared.implementation_->lossless_isomorphism_enabled ||
      options.enable_canonical_public_dag !=
          prepared.implementation_->canonical_public_dag_enabled) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  struct PreparedLayoutReference {
    DenseLayout *pointer;
    [[nodiscard]] DenseLayout &value() const noexcept { return *pointer; }
  } layout{&prepared.implementation_->layout};
  const auto initialization_started = std::chrono::steady_clock::now();
  PostflopCheckpoint checkpoint;
  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers buffers;
  const bool out_of_core = options.memory_backend == MemoryPrototype::OutOfCore;
  if (resume_from != nullptr) {
    if (resume_from->game_fingerprint != layout.value().fingerprint ||
        resume_from->averaging_delay != options.averaging_delay ||
        resume_from->state_precision != options.state_precision ||
        resume_from->completed_iterations > options.iterations ||
        resume_from->action_count != layout.value().actions) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    checkpoint = *resume_from;
  } else {
    checkpoint.game_fingerprint = layout.value().fingerprint;
    checkpoint.averaging_delay = options.averaging_delay;
    checkpoint.action_count = layout.value().actions;
    checkpoint.state_precision = options.state_precision;
  }
  if (out_of_core) {
    std::string backing_file =
        resume_from != nullptr ? checkpoint.external_buffer_file : options.backing_file;
    if (backing_file.empty() ||
        (resume_from != nullptr && checkpoint.external_buffer_file.empty())) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    if (resume_from == nullptr) {
      std::error_code absolute_error;
      const auto absolute_path = std::filesystem::absolute(backing_file, absolute_error);
      if (absolute_error) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::IoFailure);
      }
      backing_file = absolute_path.lexically_normal().string();
    }
    mapped = std::make_unique<PagedActionFile>(
        backing_file, static_cast<std::size_t>(layout.value().actions), resume_from == nullptr);
    if (!mapped->valid()) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    checkpoint.external_buffer_file = backing_file;
    checkpoint.cumulative_regret.clear();
    checkpoint.cumulative_strategy.clear();
    checkpoint.cumulative_regret_float32.clear();
    checkpoint.cumulative_strategy_float32.clear();
    buffers = mapped->buffers();
  } else {
    const bool float32_state = options.state_precision == PostflopStatePrecision::Float32;
    const bool checkpoint_size_matches =
        float32_state
            ? checkpoint.cumulative_regret_float32.size() == layout.value().actions &&
                  checkpoint.cumulative_strategy_float32.size() == layout.value().actions &&
                  checkpoint.cumulative_regret.empty() && checkpoint.cumulative_strategy.empty()
            : checkpoint.cumulative_regret.size() == layout.value().actions &&
                  checkpoint.cumulative_strategy.size() == layout.value().actions &&
                  checkpoint.cumulative_regret_float32.empty() &&
                  checkpoint.cumulative_strategy_float32.empty();
    if (resume_from != nullptr &&
        (!checkpoint_size_matches || !checkpoint.external_buffer_file.empty())) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    if (resume_from == nullptr) {
      if (float32_state) {
        checkpoint.cumulative_regret_float32.resize(
            static_cast<std::size_t>(layout.value().actions), 0.0F);
        checkpoint.cumulative_strategy_float32.resize(
            static_cast<std::size_t>(layout.value().actions), 0.0F);
      } else {
        checkpoint.cumulative_regret.resize(static_cast<std::size_t>(layout.value().actions), 0.0);
        checkpoint.cumulative_strategy.resize(static_cast<std::size_t>(layout.value().actions),
                                              0.0);
      }
    }
    if (float32_state) {
      buffers = {nullptr,
                 nullptr,
                 checkpoint.cumulative_regret_float32.size(),
                 nullptr,
                 checkpoint.cumulative_regret_float32.data(),
                 checkpoint.cumulative_strategy_float32.data()};
    } else {
      buffers = {checkpoint.cumulative_regret.data(), checkpoint.cumulative_strategy.data(),
                 checkpoint.cumulative_regret.size()};
    }
  }

  PostflopSolveResult result;
  result.public_tree = layout.value().tree.stats;
  result.canonical_public_nodes = layout.value().canonical_public_graph.nodes.size();
  result.information_sets = layout.value().information_sets;
  result.actions = layout.value().actions;
  std::vector<double> deferred_regret_delta;
  // The deferred regret delta (8 B x actions = 667 MB for th7d6s) is only
  // needed when the runner's traversal will actually accumulate into it: the
  // canonical-DAG path and the non-direct-action-bases physical path use it,
  // but the per-player direct-action-bases path applies regrets immediately
  // (make_dense_traversal_runner passes nullptr there), so allocating it for
  // that path would waste 667 MB of peak RSS.
  const bool compact_dag =
      layout.value().uses_canonical_public_dag &&
      layout.value().active_combos.size() <= compact_combo_capacity;
  const bool deferred_delta_needed =
      layout.value().uses_isomorphic_infosets &&
      !(layout.value().uses_direct_action_bases && !compact_dag);
  if (deferred_delta_needed) {
    deferred_regret_delta.resize(static_cast<std::size_t>(layout.value().actions), 0.0);
  }
  std::unique_ptr<PreparedRootLock> prepared_root_lock;
  if (options.diagnostic_root_lock != nullptr) {
    auto prepared_lock = prepare_root_lock(*options.diagnostic_root_lock, layout.value());
    if (!prepared_lock) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(prepared_lock.error());
    }
    prepared_root_lock = std::move(prepared_lock.value());
  }
  auto traversal = make_dense_traversal_runner(
      layout.value(), buffers, deferred_delta_needed ? &deferred_regret_delta : nullptr,
      options.parallel_action_depth, prepared_root_lock.get());
  result.timings.layout_seconds = 0.0;
  result.timings.initialization_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - initialization_started)
          .count();
  if (checkpoint.completed_iterations == options.iterations) {
    const auto certification_started = std::chrono::steady_clock::now();
    const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations,
                                       prepared_root_lock.get());
    result.timings.certification_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - certification_started)
            .count();
    if (!certification) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(certification.error());
    }
    result.convergence.push_back(certification.value());
    if (options.progress_callback) {
      options.progress_callback(certification.value());
    }
    if (mapped && !mapped->flush()) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    if (options.checkpoint_callback &&
        !options.checkpoint_callback(certification.value(), checkpoint)) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
  }
  for (std::uint64_t iteration = checkpoint.completed_iterations + 1U;
       iteration <= options.iterations; ++iteration) {
    const auto traversal_started = std::chrono::steady_clock::now();
    const double strategy_weight = iteration > options.averaging_delay
                                       ? static_cast<double>(iteration - options.averaging_delay)
                                       : 0.0;
    for (std::uint8_t player = 0; player < 2U; ++player) {
      const auto traversed = traversal->cfr(player, strategy_weight);
      if (!traversed) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(traversed.error());
      }
      const auto regret_application_started = std::chrono::steady_clock::now();
      const auto applied = traversal->apply_deferred_regrets();
      result.timings.regret_application_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        regret_application_started)
              .count();
      if (!applied) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(applied.error());
      }
    }
    result.timings.traversal_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - traversal_started).count();
    checkpoint.completed_iterations = iteration;
    const auto control = options.control_callback ? options.control_callback(iteration)
                                                  : PostflopControlCommand::Continue;
    const bool stopping = control != PostflopControlCommand::Continue;
    bool converged = false;
    if (iteration % options.certification_interval == 0U || iteration == options.iterations ||
        stopping) {
      const auto certification_started = std::chrono::steady_clock::now();
      const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations,
                                       prepared_root_lock.get());
      result.timings.certification_seconds +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - certification_started)
              .count();
      if (!certification) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(certification.error());
      }
      result.convergence.push_back(certification.value());
      if (options.target_normalized_nash_conv) {
        converged =
            certification.value().normalized_nash_conv <= *options.target_normalized_nash_conv;
      } else if (options.target_normalized_max_deviation) {
        const auto deviation =
            normalized_max_deviation_gain(certification.value(), config.initial_pot);
        if (!deviation) {
          return Result<PostflopSolveResult, PostflopSolverError>::failure(deviation.error());
        }
        converged = deviation.value() <= *options.target_normalized_max_deviation;
      }
      if (options.progress_callback) {
        options.progress_callback(certification.value());
      }
      if (mapped && !mapped->flush()) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::IoFailure);
      }
      if (options.checkpoint_callback &&
          !options.checkpoint_callback(certification.value(), checkpoint)) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::IoFailure);
      }
    }
    if (converged) {
      result.stop_reason = PostflopStopReason::Converged;
      break;
    }
    if (stopping) {
      result.stop_reason = control == PostflopControlCommand::Pause ? PostflopStopReason::Paused
                                                                    : PostflopStopReason::Cancelled;
      break;
    }
  }
  const auto finalization_started = std::chrono::steady_clock::now();
  if (out_of_core) {
    for (std::size_t index = 0; index < buffers.count; ++index) {
      if (!std::isfinite(buffers.regret_at(index)) || !std::isfinite(buffers.strategy_at(index))) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::NumericalFailure);
      }
    }
  } else if ((!checkpoint.cumulative_regret.empty() &&
              (!finite_vector(checkpoint.cumulative_regret) ||
               !finite_vector(checkpoint.cumulative_strategy))) ||
             (!checkpoint.cumulative_regret_float32.empty() &&
              (!finite_vector(checkpoint.cumulative_regret_float32) ||
               !finite_vector(checkpoint.cumulative_strategy_float32)))) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::NumericalFailure);
  }
  result.traversed_nodes = traversal->traversed_nodes();
  const auto normalization_error = measure_canonical_normalization_error(layout.value(), buffers);
  if (!normalization_error) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(normalization_error.error());
  }
  result.maximum_normalization_error =
      std::max(traversal->maximum_normalization_error(), normalization_error.value());
  result.checkpoint = std::move(checkpoint);
  result.timings.finalization_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - finalization_started)
          .count();
  result.timings.run_solver_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - initialization_started)
          .count();
  result.timings.total_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - solve_started).count();
  return Result<PostflopSolveResult, PostflopSolverError>::success(std::move(result));
}

Result<double, PostflopSolverError>
normalized_max_deviation_gain(const PostflopCertification &certification, const Money initial_pot) {
  const double initial_pot_antes = static_cast<double>(initial_pot.units()) / units_per_ante;
  if (!(initial_pot_antes > 0.0) || !std::isfinite(initial_pot_antes)) {
    return Result<double, PostflopSolverError>::failure(PostflopSolverError::InvalidConfiguration);
  }
  double maximum_gain = 0.0;
  for (std::size_t player = 0; player < certification.profile_value_antes.size(); ++player) {
    const double gain =
        certification.best_response_value_antes[player] - certification.profile_value_antes[player];
    if (!std::isfinite(gain) || gain < -1.0e-10) {
      return Result<double, PostflopSolverError>::failure(PostflopSolverError::NumericalFailure);
    }
    maximum_gain = std::max(maximum_gain, std::max(0.0, gain));
  }
  const double normalized = maximum_gain / initial_pot_antes;
  return std::isfinite(normalized)
             ? Result<double, PostflopSolverError>::success(normalized)
             : Result<double, PostflopSolverError>::failure(PostflopSolverError::NumericalFailure);
}

Result<PostflopCertification, PostflopSolverError>
certify_postflop_checkpoint(const PostflopTreeConfig &config,
                            const PostflopCheckpoint &checkpoint) {
  return certify_postflop_checkpoint(config, make_uniform_postflop_ranges(), checkpoint);
}

Result<PostflopCertification, PostflopSolverError>
certify_postflop_checkpoint(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                            const PostflopCheckpoint &checkpoint) {
  auto layout = build_layout(config, ranges);
  if (!layout) {
    return Result<PostflopCertification, PostflopSolverError>::failure(layout.error());
  }
  if (checkpoint.game_fingerprint != layout.value().fingerprint ||
      checkpoint.action_count != layout.value().actions) {
    return Result<PostflopCertification, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  if (!checkpoint.external_buffer_file.empty()) {
    PagedActionFile mapped(checkpoint.external_buffer_file,
                           static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped.valid()) {
      return Result<PostflopCertification, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    return certify(layout.value(), mapped.buffers(), checkpoint.completed_iterations, nullptr);
  }
  const auto buffers = in_memory_checkpoint_buffers(checkpoint);
  if (!buffers) {
    return Result<PostflopCertification, PostflopSolverError>::failure(buffers.error());
  }
  return certify(layout.value(), buffers.value(), checkpoint.completed_iterations, nullptr);
}

Result<PostflopStrategyQuery, PostflopSolverError>
query_postflop_strategy(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint,
                        const NodeId public_node, const ComboId combo) {
  return query_postflop_strategy(config, make_uniform_postflop_ranges(), checkpoint, public_node,
                                 combo);
}

Result<PostflopStrategyQuery, PostflopSolverError>
query_postflop_strategy(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                        const PostflopCheckpoint &checkpoint, const NodeId public_node,
                        const ComboId combo) {
  auto layout = build_layout(config, ranges);
  if (!layout || public_node >= layout.value().tree.nodes.size() || combo >= combo_count) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &decision = layout.value().decisions[static_cast<std::size_t>(public_node)];
  if (!decision.present || checkpoint.game_fingerprint != layout.value().fingerprint ||
      checkpoint.action_count != layout.value().actions) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers query_buffers;
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    query_buffers = mapped->buffers();
  } else {
    const auto in_memory = in_memory_checkpoint_buffers(checkpoint);
    if (!in_memory) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(in_memory.error());
    }
    query_buffers = in_memory.value();
  }
  return query_strategy_from_layout(layout.value(), query_buffers, public_node, combo);
}

Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>
query_postflop_strategies(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                          const PostflopCheckpoint &checkpoint, const NodeId public_node) {
  auto layout = build_layout(config, ranges);
  if (!layout || public_node >= layout.value().tree.nodes.size()) {
    return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &decision = layout.value().decisions[static_cast<std::size_t>(public_node)];
  if (!decision.present || checkpoint.game_fingerprint != layout.value().fingerprint ||
      checkpoint.action_count != layout.value().actions) {
    return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers query_buffers;
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    query_buffers = mapped->buffers();
  } else {
    const auto in_memory = in_memory_checkpoint_buffers(checkpoint);
    if (!in_memory) {
      return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
          in_memory.error());
    }
    query_buffers = in_memory.value();
  }
  std::vector<PostflopStrategyQuery> result;
  const auto &board = layout.value().boards[decision.board_index];
  result.reserve(board.legal_combos.size());
  for (const ComboId combo : board.legal_combos) {
    auto query = query_strategy_from_layout(layout.value(), query_buffers, public_node, combo);
    if (!query) {
      return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
          query.error());
    }
    result.push_back(std::move(query.value()));
  }
  return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::success(
      std::move(result));
}

Result<PostflopLayoutEstimate, PostflopSolverError>
estimate_postflop_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges) {
  if (!validate_postflop_ranges(config, ranges)) {
    return Result<PostflopLayoutEstimate, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  auto layout = build_layout(config, ranges, true, true);
  if (!layout) {
    return Result<PostflopLayoutEstimate, PostflopSolverError>::failure(layout.error());
  }
  PostflopLayoutEstimate estimate;
  estimate.physical_public_tree = layout.value().tree.stats;
  estimate.canonical_public_nodes = layout.value().canonical_public_graph.nodes.size();
  estimate.information_sets = layout.value().information_sets;
  estimate.actions = layout.value().actions;
  if (estimate.actions > std::numeric_limits<std::uint64_t>::max() / sizeof(double)) {
    return Result<PostflopLayoutEstimate, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  estimate.regret_bytes = estimate.actions * sizeof(double);
  estimate.strategy_bytes = estimate.actions * sizeof(double);
  return Result<PostflopLayoutEstimate, PostflopSolverError>::success(estimate);
}

static Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node_with_layout(DenseLayout &dense, const PostflopCheckpoint &checkpoint,
                                  const NodeId public_node) {
  if (public_node >= dense.tree.nodes.size()) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  const auto &target = dense.tree.nodes[static_cast<std::size_t>(public_node)];
  if (target.kind != PublicNodeKind::Decision || checkpoint.game_fingerprint != dense.fingerprint ||
      checkpoint.action_count != dense.actions) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }

  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers buffers;
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    buffers = mapped->buffers();
  } else {
    const auto in_memory = in_memory_checkpoint_buffers(checkpoint);
    if (!in_memory) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(in_memory.error());
    }
    buffers = in_memory.value();
  }

  struct ParentStep {
    NodeId parent{std::numeric_limits<NodeId>::max()};
    std::uint32_t edge{0};
  };
  std::vector<ParentStep> parents(dense.tree.nodes.size());
  for (const auto &node : dense.tree.nodes) {
    for (std::size_t edge = 0; edge < node.edges.size(); ++edge) {
      parents[static_cast<std::size_t>(node.edges[edge].child)] = {
          node.id, static_cast<std::uint32_t>(edge)};
    }
  }
  std::vector<ParentStep> path;
  for (NodeId cursor = public_node; cursor != dense.tree.root;) {
    const auto step = parents[static_cast<std::size_t>(cursor)];
    if (step.parent == std::numeric_limits<NodeId>::max()) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    path.push_back(step);
    cursor = step.parent;
  }
  std::ranges::reverse(path);

  auto reach = dense.initial_reach;
  for (const auto step : path) {
    const auto &parent = dense.tree.nodes[static_cast<std::size_t>(step.parent)];
    const auto &edge = parent.edges[step.edge];
    if (edge.kind == PublicEdgeKind::ChanceCard) {
      const auto card_mask = edge.chance_card.mask();
      for (std::size_t combo = 0; combo < combo_count; ++combo) {
        if ((dense.combo_masks[combo] & card_mask) != 0U) {
          reach[0][combo] = 0.0;
          reach[1][combo] = 0.0;
        }
      }
      continue;
    }
    const auto &decision = dense.decisions[static_cast<std::size_t>(parent.id)];
    const auto &board = dense.boards[decision.board_index];
    for (const ComboId combo : board.legal_combos) {
      const auto query = query_strategy_from_layout(dense, buffers, parent.id, combo);
      if (!query || step.edge >= query.value().probabilities.size()) {
        return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      reach[decision.player][combo] *= query.value().probabilities[step.edge];
    }
  }

  const auto actor = static_cast<std::size_t>(target.state.player_to_act);
  const auto opponent = 1U - actor;
  const auto &decision = dense.decisions[static_cast<std::size_t>(public_node)];
  const auto &board = dense.boards[decision.board_index];
  const auto board_cards = cards_from_mask(board.mask);
  if (board_cards.size() < 3U || board_cards.size() > 5U) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  const auto evaluate_current =
      [&board_cards](const Combo &combo) -> Result<HandValue, PostflopSolverError> {
    std::vector<CardId> cards{combo.first, combo.second};
    cards.insert(cards.end(), board_cards.begin(), board_cards.end());
    if (cards.size() == 5U) {
      std::array<CardId, 5> five{};
      std::ranges::copy(cards, five.begin());
      const auto value = evaluate_five(five);
      return value ? Result<HandValue, PostflopSolverError>::success(value.value())
                   : Result<HandValue, PostflopSolverError>::failure(
                         PostflopSolverError::EquityFailure);
    }
    HandValue best{};
    bool initialized = false;
    const auto choose_five = [&](const auto &self, const std::size_t start,
                                 std::array<CardId, 5> &five,
                                 const std::size_t used) -> Result<bool, PostflopSolverError> {
      if (used == five.size()) {
        const auto value = evaluate_five(five);
        if (!value) {
          return Result<bool, PostflopSolverError>::failure(PostflopSolverError::EquityFailure);
        }
        if (!initialized || value.value() > best) {
          best = value.value();
          initialized = true;
        }
        return Result<bool, PostflopSolverError>::success(true);
      }
      for (std::size_t index = start; index <= cards.size() - (five.size() - used); ++index) {
        five[used] = cards[index];
        const auto result = self(self, index + 1U, five, used + 1U);
        if (!result) {
          return result;
        }
      }
      return Result<bool, PostflopSolverError>::success(true);
    };
    std::array<CardId, 5> five{};
    const auto selected = choose_five(choose_five, 0U, five, 0U);
    return selected && initialized ? Result<HandValue, PostflopSolverError>::success(best)
                                   : Result<HandValue, PostflopSolverError>::failure(
                                         PostflopSolverError::EquityFailure);
  };

  std::array<double, combo_count> equity_won{};
  std::array<double, combo_count> equity_total{};
  std::vector<CardId> public_candidates;
  for (std::uint8_t card = 0; card < 36U; ++card) {
    const auto candidate = CardId::from_index(card).value();
    if ((candidate.mask() & board.mask) == 0U) {
      public_candidates.push_back(candidate);
    }
  }
  const auto missing = 5U - board_cards.size();
  const auto runout_count = missing == 0U ? 1U : public_candidates.size();
  for (std::size_t first = 0; first < runout_count; ++first) {
    const auto second_begin = missing == 2U ? first + 1U : first;
    const auto second_end = missing == 2U ? public_candidates.size() : second_begin + 1U;
    for (std::size_t second = second_begin; second < second_end; ++second) {
      std::uint64_t runout_mask = 0U;
      if (missing >= 1U) {
        runout_mask |= public_candidates[first].mask();
      }
      if (missing == 2U) {
        runout_mask |= public_candidates[second].mask();
      }
      std::array<HandValue, combo_count> values{};
      std::array<bool, combo_count> valid{};
      for (const ComboId combo_id : board.legal_combos) {
        if ((dense.combo_masks[combo_id] & runout_mask) != 0U ||
            (!(reach[actor][combo_id] > 0.0) && !(reach[opponent][combo_id] > 0.0))) {
          continue;
        }
        const auto &combo = dense.combos[combo_id];
        std::array<CardId, 7> cards{combo.first,    combo.second, board_cards[0], board_cards[1],
                                    board_cards[2], CardId{},     CardId{}};
        cards[5] = board_cards.size() >= 4U ? board_cards[3] : public_candidates[first];
        cards[6] = board_cards.size() == 5U   ? board_cards[4]
                   : board_cards.size() == 4U ? public_candidates[first]
                                              : public_candidates[second];
        const auto value = evaluate_seven(cards);
        if (!value) {
          return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
              PostflopSolverError::EquityFailure);
        }
        values[combo_id] = value.value();
        valid[combo_id] = true;
      }
      for (const ComboId hero_id : board.legal_combos) {
        if (!valid[hero_id] || !(reach[actor][hero_id] > 0.0)) {
          continue;
        }
        for (const ComboId villain_id : board.legal_combos) {
          const double villain_weight = reach[opponent][villain_id];
          if (!valid[villain_id] || !(villain_weight > 0.0) ||
              (dense.combo_masks[hero_id] & dense.combo_masks[villain_id]) != 0U) {
            continue;
          }
          equity_total[hero_id] += villain_weight;
          if (values[hero_id] > values[villain_id]) {
            equity_won[hero_id] += villain_weight;
          } else if (values[hero_id] == values[villain_id]) {
            equity_won[hero_id] += villain_weight * 0.5;
          }
        }
      }
    }
  }

  PostflopNodeAnalysis analysis;
  analysis.public_node = public_node;
  analysis.player_to_act = target.state.player_to_act;
  double compatible_pair_mass = 0.0;
  for (const ComboId first : board.legal_combos) {
    if (!(reach[0][first] > 0.0)) {
      continue;
    }
    for (const ComboId second : board.legal_combos) {
      if ((dense.combo_masks[first] & dense.combo_masks[second]) == 0U) {
        compatible_pair_mass += reach[0][first] * reach[1][second];
      }
    }
  }
  const double public_reach_probability = compatible_pair_mass / dense.initial_normalization;
  if (!(public_reach_probability > 0.0) || !std::isfinite(public_reach_probability)) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
        PostflopSolverError::NumericalFailure);
  }
  for (std::uint8_t player = 0; player < 2U; ++player) {
    DenseTraversal<combo_count> traversal(dense, buffers);
    const auto values = traversal.policy_from_physical_node(public_node, player,
                                                            {&reach[0], &reach[1]});
    if (!values) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(values.error());
    }
    double weighted_value = 0.0;
    for (const ComboId combo : board.legal_combos) {
      weighted_value += values.value()[combo] * reach[player][combo];
    }
    analysis.profile_value_antes[player] = weighted_value / public_reach_probability;
    analysis.gto_plus_ev_antes[player] =
        analysis.profile_value_antes[player] +
        static_cast<double>(target.state.initial_pot_contributions[player].units()) /
            units_per_ante;
    if (!std::isfinite(analysis.gto_plus_ev_antes[player])) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
          PostflopSolverError::NumericalFailure);
    }
  }
  for (const auto &edge : target.edges) {
    analysis.actions.push_back(edge.action);
  }
  analysis.action_frequencies.assign(target.edges.size(), 0.0);
  double aggregate_weight = 0.0;
  for (const ComboId hero_id : board.legal_combos) {
    if (!(reach[actor][hero_id] > 0.0)) {
      continue;
    }
    const auto &hero = dense.combos[hero_id];
    const auto hero_mask = dense.combo_masks[hero_id];
    double compatible_opponent_weight = 0.0;
    for (const ComboId villain_id : board.legal_combos) {
      if ((hero_mask & dense.combo_masks[villain_id]) == 0U) {
        compatible_opponent_weight += reach[opponent][villain_id];
      }
    }
    const double combo_weight = reach[actor][hero_id] * compatible_opponent_weight;
    if (!(combo_weight > 0.0)) {
      continue;
    }
    const auto query = query_strategy_from_layout(dense, buffers, public_node, hero_id);
    const auto current_value = evaluate_current(hero);
    if (!query || !current_value) {
      return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(
          PostflopSolverError::EquityFailure);
    }
    for (std::size_t action = 0; action < analysis.action_frequencies.size(); ++action) {
      analysis.action_frequencies[action] += combo_weight * query.value().probabilities[action];
    }
    aggregate_weight += combo_weight;

    analysis.combos.push_back(
        {hero_id, reach[actor][hero_id],
         equity_total[hero_id] > 0.0 ? equity_won[hero_id] / equity_total[hero_id] : 0.0,
         current_value.value().category, query.value().probabilities});
  }
  if (aggregate_weight > 0.0) {
    for (double &frequency : analysis.action_frequencies) {
      frequency /= aggregate_weight;
    }
  }
  return Result<PostflopNodeAnalysis, PostflopSolverError>::success(std::move(analysis));
}

Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                      const PostflopCheckpoint &checkpoint, const NodeId public_node) {
  auto layout = build_layout(config, ranges);
  if (!layout) {
    return Result<PostflopNodeAnalysis, PostflopSolverError>::failure(layout.error());
  }
  return analyze_postflop_node_with_layout(layout.value(), checkpoint, public_node);
}

Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node(PostflopPreparedTree &prepared, const PostflopCheckpoint &checkpoint,
                      const NodeId public_node) {
  auto &layout = prepared.implementation_->analysis_layout
                     ? *prepared.implementation_->analysis_layout
                     : prepared.implementation_->layout;
  return analyze_postflop_node_with_layout(layout, checkpoint, public_node);
}

Result<std::string, PostflopSolverError>
serialize_postflop_checkpoint(const PostflopCheckpoint &checkpoint) {
  if (checkpoint.major != PostflopCheckpoint::format_major ||
      checkpoint.minor != PostflopCheckpoint::format_minor || checkpoint.game_fingerprint.empty() ||
      checkpoint.game_fingerprint.size() > 1'024U || checkpoint.action_count == 0U ||
      checkpoint.action_count != checkpoint.cumulative_regret.size() ||
      checkpoint.cumulative_regret.size() != checkpoint.cumulative_strategy.size() ||
      !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<std::string, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  std::ostringstream output;
  output << "GTOSD_POSTFLOP_CHECKPOINT " << checkpoint.major << ' ' << checkpoint.minor << '\n'
         << checkpoint.game_fingerprint << '\n'
         << checkpoint.completed_iterations << ' ' << checkpoint.averaging_delay << ' '
         << checkpoint.cumulative_regret.size() << '\n';
  for (std::size_t index = 0; index < checkpoint.cumulative_regret.size(); ++index) {
    output << double_bits(checkpoint.cumulative_regret[index]) << ' '
           << double_bits(checkpoint.cumulative_strategy[index]) << '\n';
  }
  return Result<std::string, PostflopSolverError>::success(output.str());
}

Result<PostflopCheckpoint, PostflopSolverError>
deserialize_postflop_checkpoint(const std::string &serialized) {
  std::istringstream input(serialized);
  std::string magic;
  PostflopCheckpoint checkpoint;
  std::size_t count = 0;
  if (!(input >> magic >> checkpoint.major >> checkpoint.minor) ||
      magic != "GTOSD_POSTFLOP_CHECKPOINT") {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  if (checkpoint.major != PostflopCheckpoint::format_major ||
      checkpoint.minor != PostflopCheckpoint::format_minor) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::UnsupportedCheckpointVersion);
  }
  if (!(input >> checkpoint.game_fingerprint >> checkpoint.completed_iterations >>
        checkpoint.averaging_delay >> count) ||
      checkpoint.game_fingerprint.size() > 1'024U || count == 0U ||
      count > serialized.size() / 4U) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  checkpoint.cumulative_regret.resize(count);
  checkpoint.cumulative_strategy.resize(count);
  checkpoint.action_count = count;
  for (std::size_t index = 0; index < count; ++index) {
    std::uint64_t regret = 0;
    std::uint64_t strategy = 0;
    if (!(input >> regret >> strategy)) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    checkpoint.cumulative_regret[index] = bits_double(regret);
    checkpoint.cumulative_strategy[index] = bits_double(strategy);
  }
  input >> std::ws;
  if (!input.eof() || !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  return Result<PostflopCheckpoint, PostflopSolverError>::success(std::move(checkpoint));
}

Result<bool, PostflopSolverError> save_postflop_checkpoint(const PostflopCheckpoint &checkpoint,
                                                           const std::string &path) {
  if (!checkpoint.external_buffer_file.empty()) {
    if (path.empty() || checkpoint.game_fingerprint.empty() || checkpoint.action_count == 0U ||
        checkpoint.game_fingerprint.size() > 1'024U ||
        checkpoint.major != PostflopCheckpoint::format_major ||
        checkpoint.minor != PostflopCheckpoint::format_minor ||
        !checkpoint.cumulative_regret.empty() || !checkpoint.cumulative_strategy.empty() ||
        checkpoint.action_count >
            std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::InvalidCheckpoint);
    }
    std::error_code size_error;
    const auto expected_size = checkpoint.action_count * 2U * sizeof(double);
    if (std::filesystem::file_size(checkpoint.external_buffer_file, size_error) != expected_size ||
        size_error) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    const std::filesystem::path target(path);
    const auto temporary = target.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "GTOSD_POSTFLOP_EXTERNAL " << checkpoint.major << ' ' << checkpoint.minor << '\n'
           << checkpoint.game_fingerprint << '\n'
           << checkpoint.completed_iterations << ' ' << checkpoint.averaging_delay << ' '
           << checkpoint.action_count << '\n'
           << checkpoint.external_buffer_file << '\n';
    output.flush();
    output.close();
    if (!output || !atomic_replace(temporary, target)) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    return Result<bool, PostflopSolverError>::success(true);
  }
  if (path.empty() || checkpoint.game_fingerprint.empty() ||
      checkpoint.game_fingerprint.size() > 1'024U || checkpoint.action_count == 0U ||
      checkpoint.action_count != checkpoint.cumulative_regret.size() ||
      checkpoint.cumulative_regret.size() != checkpoint.cumulative_strategy.size() ||
      !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<bool, PostflopSolverError>::failure(
        path.empty() ? PostflopSolverError::InvalidConfiguration
                     : PostflopSolverError::InvalidCheckpoint);
  }
  const std::filesystem::path target(path);
  const auto temporary = target.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    constexpr std::string_view magic = "GTOSD_POSTFLOP_BINARY_1";
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    std::uint64_t checksum = 1469598103934665603ULL;
    checksum_bytes(checksum, magic.data(), magic.size());
    const auto fingerprint_size = static_cast<std::uint32_t>(checkpoint.game_fingerprint.size());
    const auto value_count = static_cast<std::uint64_t>(checkpoint.cumulative_regret.size());
    if (!write_binary(output, checkpoint.major, checksum) ||
        !write_binary(output, checkpoint.minor, checksum) ||
        !write_binary(output, fingerprint_size, checksum)) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    output.write(checkpoint.game_fingerprint.data(),
                 static_cast<std::streamsize>(checkpoint.game_fingerprint.size()));
    checksum_bytes(checksum, checkpoint.game_fingerprint.data(),
                   checkpoint.game_fingerprint.size());
    if (!write_binary(output, checkpoint.completed_iterations, checksum) ||
        !write_binary(output, checkpoint.averaging_delay, checksum) ||
        !write_binary(output, value_count, checksum)) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
    for (const double value : checkpoint.cumulative_regret) {
      if (!write_binary(output, value, checksum)) {
        return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
      }
    }
    for (const double value : checkpoint.cumulative_strategy) {
      if (!write_binary(output, value, checksum)) {
        return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
      }
    }
    output.write(reinterpret_cast<const char *>(&checksum), sizeof(checksum));
    output.flush();
    if (!output) {
      return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
    }
  }
  if (!atomic_replace(temporary, target)) {
    return Result<bool, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
  }
  return Result<bool, PostflopSolverError>::success(true);
}

Result<PostflopCheckpoint, PostflopSolverError> load_postflop_checkpoint(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(PostflopSolverError::IoFailure);
  }
  constexpr std::string_view external_magic = "GTOSD_POSTFLOP_EXTERNAL";
  std::string prefix(external_magic.size(), '\0');
  input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
  input.clear();
  input.seekg(0);
  if (prefix == external_magic) {
    std::string magic;
    PostflopCheckpoint checkpoint;
    if (!(input >> magic >> checkpoint.major >> checkpoint.minor >> checkpoint.game_fingerprint >>
          checkpoint.completed_iterations >> checkpoint.averaging_delay >>
          checkpoint.action_count) ||
        magic != external_magic || checkpoint.major != PostflopCheckpoint::format_major ||
        checkpoint.minor != PostflopCheckpoint::format_minor ||
        checkpoint.game_fingerprint.size() > 1'024U || checkpoint.action_count == 0U) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::getline(input, checkpoint.external_buffer_file);
    input >> std::ws;
    if (!input.eof() || checkpoint.external_buffer_file.empty() ||
        checkpoint.action_count >
            std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    std::error_code size_error;
    const auto expected_size = checkpoint.action_count * 2U * sizeof(double);
    if (std::filesystem::file_size(checkpoint.external_buffer_file, size_error) != expected_size ||
        size_error) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
    return Result<PostflopCheckpoint, PostflopSolverError>::success(std::move(checkpoint));
  }
  constexpr std::string_view expected_magic = "GTOSD_POSTFLOP_BINARY_1";
  std::string magic(expected_magic.size(), '\0');
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (!input || magic != expected_magic) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  std::uint64_t checksum = 1469598103934665603ULL;
  checksum_bytes(checksum, magic.data(), magic.size());
  PostflopCheckpoint checkpoint;
  std::uint32_t fingerprint_size = 0;
  std::uint64_t value_count = 0;
  if (!read_binary(input, checkpoint.major, checksum) ||
      !read_binary(input, checkpoint.minor, checksum) ||
      !read_binary(input, fingerprint_size, checksum) || fingerprint_size == 0U ||
      fingerprint_size > 1'024U) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  if (checkpoint.major != PostflopCheckpoint::format_major ||
      checkpoint.minor != PostflopCheckpoint::format_minor) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::UnsupportedCheckpointVersion);
  }
  checkpoint.game_fingerprint.resize(fingerprint_size);
  input.read(checkpoint.game_fingerprint.data(),
             static_cast<std::streamsize>(checkpoint.game_fingerprint.size()));
  if (!input) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  checksum_bytes(checksum, checkpoint.game_fingerprint.data(), checkpoint.game_fingerprint.size());
  if (!read_binary(input, checkpoint.completed_iterations, checksum) ||
      !read_binary(input, checkpoint.averaging_delay, checksum) ||
      !read_binary(input, value_count, checksum) || value_count == 0U ||
      value_count > std::numeric_limits<std::size_t>::max() ||
      value_count > std::numeric_limits<std::uint64_t>::max() / (2U * sizeof(double))) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  const auto values_position = input.tellg();
  if (values_position == std::streampos(-1)) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  const auto values_offset =
      static_cast<std::uint64_t>(static_cast<std::streamoff>(values_position));
  std::error_code file_size_error;
  const auto file_size = std::filesystem::file_size(path, file_size_error);
  const auto payload_size = value_count * 2U * sizeof(double) + sizeof(std::uint64_t);
  if (file_size_error || values_offset > std::numeric_limits<std::uint64_t>::max() - payload_size ||
      file_size != values_offset + payload_size) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  checkpoint.cumulative_regret.resize(static_cast<std::size_t>(value_count));
  checkpoint.cumulative_strategy.resize(static_cast<std::size_t>(value_count));
  checkpoint.action_count = value_count;
  for (double &value : checkpoint.cumulative_regret) {
    if (!read_binary(input, value, checksum)) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
  }
  for (double &value : checkpoint.cumulative_strategy) {
    if (!read_binary(input, value, checksum)) {
      return Result<PostflopCheckpoint, PostflopSolverError>::failure(
          PostflopSolverError::InvalidCheckpoint);
    }
  }
  std::uint64_t stored_checksum = 0;
  input.read(reinterpret_cast<char *>(&stored_checksum), sizeof(stored_checksum));
  input.peek();
  if (!input.eof() || stored_checksum != checksum || !finite_vector(checkpoint.cumulative_regret) ||
      !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<PostflopCheckpoint, PostflopSolverError>::failure(
        PostflopSolverError::InvalidCheckpoint);
  }
  return Result<PostflopCheckpoint, PostflopSolverError>::success(std::move(checkpoint));
}

const char *postflop_solver_error_name(const PostflopSolverError error) noexcept {
  switch (error) {
  case PostflopSolverError::InvalidConfiguration:
    return "invalid_configuration";
  case PostflopSolverError::TreeFailure:
    return "tree_failure";
  case PostflopSolverError::MemoryFailure:
    return "memory_failure";
  case PostflopSolverError::EquityFailure:
    return "equity_failure";
  case PostflopSolverError::SettlementFailure:
    return "settlement_failure";
  case PostflopSolverError::NumericalFailure:
    return "numerical_failure";
  case PostflopSolverError::CheckpointMismatch:
    return "checkpoint_mismatch";
  case PostflopSolverError::InvalidCheckpoint:
    return "invalid_checkpoint";
  case PostflopSolverError::UnsupportedCheckpointVersion:
    return "unsupported_checkpoint_version";
  case PostflopSolverError::IoFailure:
    return "io_failure";
  }
  return "unknown";
}

} // namespace gtosd
