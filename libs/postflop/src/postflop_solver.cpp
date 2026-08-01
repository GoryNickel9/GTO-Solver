#include "gtosd/postflop/postflop_solver.hpp"

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
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
  layout.active_combo_index.fill(-1);
  layout.combos = all_combos();
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
    auto built_histories = build_node_histories(layout.tree);
    if (!built_histories) {
      return Result<DenseLayout, PostflopSolverError>::failure(built_histories.error());
    }
    histories = std::move(built_histories.value());
    public_history_ids = intern_public_histories(layout.tree, histories);
    const auto initial_board =
        layout.tree.nodes[static_cast<std::size_t>(layout.tree.root)].state.board_mask;
    auto exact_automorphisms = range_automorphisms(layout.combos, ranges, initial_board);
    if (!exact_automorphisms) {
      return Result<DenseLayout, PostflopSolverError>::failure(exact_automorphisms.error());
    }
    automorphisms = std::move(exact_automorphisms.value());
    canonical_infosets.reserve(layout.tree.stats.decision_nodes * 8U);
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
    const auto legal_count = board.legal_combos.size();
    if (decision.action_count == 0U || decision.action_count > maximum_action_count) {
      return Result<DenseLayout, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    if (!layout.uses_isomorphic_infosets) {
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
    for (const ComboId combo : board.legal_combos) {
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
      canonical_node.update_multiplicity.resize(board.legal_combos.size(), 0U);
      for (const ComboId combo : board.legal_combos) {
        const auto local = board.local_index[combo];
        const auto infoset_id = decision_infoset_id(layout, decision, local);
        std::uint32_t representative_private_multiplicity = 0U;
        for (const ComboId representative_combo : board.legal_combos) {
          if (decision_infoset_id(layout, decision, board.local_index[representative_combo]) ==
              infoset_id) {
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

template <std::size_t Capacity> class DenseTraversal {
  using ComboVector = TraversalComboVector<Capacity>;
  using TraversalResult = Result<ComboVector, PostflopSolverError>;

public:
  DenseTraversal(DenseLayout &layout, const ActionBuffers buffers,
                 std::vector<double> *deferred_regret_delta = nullptr,
                 const std::uint8_t parallel_action_depth = 0U)
      : layout_(layout), buffers_(buffers), deferred_regret_delta_(deferred_regret_delta) {
    if (deferred_regret_delta_ != nullptr) {
      deferred_regret_touched_flags_.resize(deferred_regret_delta_->size(), 0U);
    }
    if (parallel_action_depth > 0U && layout_.uses_canonical_public_dag &&
        deferred_regret_delta_ != nullptr) {
      parallel_regret_delta_.resize(deferred_regret_delta_->size(), 0.0);
      parallel_worker_ =
          std::make_unique<DenseTraversal>(layout_, buffers_, &parallel_regret_delta_,
                                           static_cast<std::uint8_t>(parallel_action_depth - 1U));
      parallel_thread_ = std::jthread([this] {
        while (true) {
          std::optional<std::packaged_task<TraversalResult()>> task;
          {
            std::unique_lock lock(parallel_task_mutex_);
            parallel_task_ready_.wait(
                lock, [this] { return parallel_shutdown_ || parallel_task_.has_value(); });
            if (parallel_shutdown_ && !parallel_task_) {
              return;
            }
            task = std::move(parallel_task_);
            parallel_task_.reset();
          }
          (*task)();
        }
      });
    }
  }

  ~DenseTraversal() {
    if (parallel_thread_.joinable()) {
      {
        std::scoped_lock lock(parallel_task_mutex_);
        parallel_shutdown_ = true;
      }
      parallel_task_ready_.notify_one();
    }
  }

  Result<ComboVector, PostflopSolverError> cfr(const NodeId node_id,
                                               const std::uint8_t updating_player,
                                               const std::array<ComboVector, 2> &reach,
                                               const double strategy_weight) {
    if (layout_.uses_canonical_public_dag) {
      return cfr_canonical_parallel_entry(layout_.canonical_public_graph.root, updating_player,
                                          reach, strategy_weight);
    }
    return cfr_physical(node_id, updating_player, reach, strategy_weight);
  }

  Result<ComboVector, PostflopSolverError> policy(const NodeId node_id,
                                                  const std::uint8_t updating_player,
                                                  const std::array<ComboVector, 2> &reach,
                                                  const bool best_response) {
    if (layout_.uses_canonical_public_dag) {
      return policy_canonical(layout_.canonical_public_graph.root, updating_player, reach,
                              best_response);
    }
    return policy_physical(node_id, updating_player, reach, best_response);
  }

  Result<ComboVector, PostflopSolverError>
  policy_from_physical_node(const NodeId node_id, const std::uint8_t updating_player,
                            const std::array<ComboVector, 2> &reach) {
    return policy_physical(node_id, updating_player, reach, false);
  }

private:
  [[nodiscard]] std::size_t value_slot(const ComboId combo) const noexcept {
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
    std::array<std::array<double, maximum_action_count>, Capacity> strategies{};
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
    merge_deferred_regrets_from(*parallel_worker_, parallel_regret_delta_);
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_parallel_entry(const std::uint32_t node_id, const std::uint8_t updating_player,
                               const std::array<ComboVector, 2> &reach,
                               const double strategy_weight) {
    if (parallel_worker_ != nullptr &&
        layout_.canonical_public_graph.nodes[node_id].kind == PublicNodeKind::Decision) {
      return cfr_canonical_parallel_decision(node_id, updating_player, reach, strategy_weight);
    }
    return cfr_canonical(node_id, updating_player, reach, strategy_weight);
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_parallel_decision(const std::uint32_t node_id, const std::uint8_t updating_player,
                                  const std::array<ComboVector, 2> &reach,
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
    for (const ComboId combo : board.legal_combos) {
      strategies[value_slot(combo)] = current_strategy(decision, board.local_index[combo], false);
    }
    std::array<std::array<ComboVector, 2>, maximum_parallel_action_count> child_reaches{};
    for (std::size_t action = 0; action < action_count; ++action) {
      child_reaches[action] = reach;
      for (const ComboId combo : board.legal_combos) {
        child_reaches[action][decision.player][value_slot(combo)] *=
            strategies[value_slot(combo)][action];
      }
      child_reaches[action] =
          transform_reach(child_reaches[action],
                          canonical.edges[action].outcomes.front().physical_to_child_automorphism);
    }
    const auto &parallel_outcome = canonical.edges[0].outcomes.front();
    std::packaged_task<TraversalResult()> first_task(
        [this, child = parallel_outcome.child, updating_player, child_reach = child_reaches[0],
         strategy_weight] {
          return parallel_worker_->cfr_canonical_parallel_entry(child, updating_player, child_reach,
                                                                strategy_weight);
        });
    auto first = first_task.get_future();
    {
      std::scoped_lock lock(parallel_task_mutex_);
      parallel_task_.emplace(std::move(first_task));
    }
    parallel_task_ready_.notify_one();
    std::optional<PostflopSolverError> serial_error;
    for (std::size_t action = 1U; action < action_count; ++action) {
      const auto &outcome = canonical.edges[action].outcomes.front();
      auto child =
          cfr_canonical(outcome.child, updating_player, child_reaches[action], strategy_weight);
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

    ComboVector values{};
    for (const ComboId combo : board.legal_combos) {
      const auto slot = value_slot(combo);
      const auto local = board.local_index[combo];
      const auto &strategy = strategies[slot];
      const auto offset = decision_action_base(layout_, decision, local);
      if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += strategy[action] * action_values[action][slot];
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
          if (deferred_regret_touched_flags_[index] == 0U) {
            deferred_regret_touched_flags_[index] = 1U;
            deferred_regret_touched_.push_back(index);
          }
          (*deferred_regret_delta_)[index] +=
              multiplicity * (action_values[action][slot] - values[slot]);
          add_strategy(index, multiplicity * strategy_weight * reach[updating_player][slot] *
                                  strategy[action]);
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError> cfr_physical(const NodeId node_id,
                                                        const std::uint8_t updating_player,
                                                        const std::array<ComboVector, 2> &reach,
                                                        const double strategy_weight) {
    ++traversed_nodes_;
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    switch (node.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values(node, updating_player, reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values(node, updating_player, reach[1U - updating_player]);
    case PublicNodeKind::Chance:
      return cfr_chance(node, updating_player, reach, strategy_weight);
    case PublicNodeKind::Decision:
      return cfr_decision(node, updating_player, reach, strategy_weight);
    }
    return Result<ComboVector, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  Result<ComboVector, PostflopSolverError> policy_physical(const NodeId node_id,
                                                           const std::uint8_t updating_player,
                                                           const std::array<ComboVector, 2> &reach,
                                                           const bool best_response) {
    ++traversed_nodes_;
    const auto &node = layout_.tree.nodes[static_cast<std::size_t>(node_id)];
    switch (node.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values(node, updating_player, reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values(node, updating_player, reach[1U - updating_player]);
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
    return traversed_nodes_ +
           (parallel_worker_ == nullptr ? 0U : parallel_worker_->traversed_nodes());
  }
  [[nodiscard]] double maximum_normalization_error() const noexcept {
    return parallel_worker_ == nullptr ? maximum_normalization_error_
                                       : std::max(maximum_normalization_error_,
                                                  parallel_worker_->maximum_normalization_error());
  }

  Result<bool, PostflopSolverError> apply_deferred_regrets() {
    if (deferred_regret_delta_ == nullptr) {
      return Result<bool, PostflopSolverError>::success(true);
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
    return Result<bool, PostflopSolverError>::success(true);
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
                                                         const std::array<ComboVector, 2> &reach,
                                                         const double strategy_weight) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    switch (canonical.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values_with_payoff(canonical.board_index,
                                     canonical.fold_payoff_antes[updating_player],
                                     reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values_with_payoffs(
          canonical.board_index, canonical.showdown_payoff_antes[updating_player][2],
          canonical.showdown_payoff_antes[updating_player][1],
          canonical.showdown_payoff_antes[updating_player][0], reach[1U - updating_player]);
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
                                                            const std::array<ComboVector, 2> &reach,
                                                            const bool best_response) {
    ++traversed_nodes_;
    const auto &canonical = layout_.canonical_public_graph.nodes[node_id];
    switch (canonical.kind) {
    case PublicNodeKind::TerminalFold:
      return fold_values_with_payoff(canonical.board_index,
                                     canonical.fold_payoff_antes[updating_player],
                                     reach[1U - updating_player]);
    case PublicNodeKind::TerminalShowdown:
      return showdown_values_with_payoffs(
          canonical.board_index, canonical.showdown_payoff_antes[updating_player][2],
          canonical.showdown_payoff_antes[updating_player][1],
          canonical.showdown_payoff_antes[updating_player][0], reach[1U - updating_player]);
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
                       const std::array<ComboVector, 2> &reach, const double strategy_weight) {
    if (canonical.total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[canonical.board_index];
    const double denominator = static_cast<double>(canonical.total_legal_outcome_count - 4U);
    ComboVector values{};
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
          auto child = cfr_canonical(outcome.child, updating_player, child_reach, strategy_weight);
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
            values[value_slot(combo)] += probability * parent_values[value_slot(combo)];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError>
  cfr_canonical_decision(const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
                         const std::array<ComboVector, 2> &reach, const double strategy_weight) {
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
    for (const ComboId combo : board.legal_combos) {
      strategies[value_slot(combo)] = current_strategy(decision, board.local_index[combo], false);
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      auto child_reach = reach;
      for (const ComboId combo : board.legal_combos) {
        child_reach[decision.player][value_slot(combo)] *= strategies[value_slot(combo)][action];
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      child_reach = transform_reach(child_reach, outcome.physical_to_child_automorphism);
      auto child = cfr_canonical(outcome.child, updating_player, child_reach, strategy_weight);
      if (!child) {
        return child;
      }
      action_values[action] =
          transform_values_to_parent(child.value(), outcome.physical_to_child_automorphism);
    }

    ComboVector values{};
    for (const ComboId combo : board.legal_combos) {
      const auto slot = value_slot(combo);
      const auto local = board.local_index[combo];
      const auto &strategy = strategies[slot];
      const auto offset = decision_action_base(layout_, decision, local);
      if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += strategy[action] * action_values[action][slot];
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
          add_strategy(index, multiplicity * strategy_weight * reach[updating_player][slot] *
                                  strategy[action]);
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError>
  policy_canonical_chance(const CanonicalPublicNode &canonical, const std::uint8_t updating_player,
                          const std::array<ComboVector, 2> &reach, const bool best_response) {
    if (canonical.total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[canonical.board_index];
    const double denominator = static_cast<double>(canonical.total_legal_outcome_count - 4U);
    ComboVector values{};
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
          auto child = policy_canonical(outcome.child, updating_player, child_reach, best_response);
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
            values[value_slot(combo)] += probability * parent_values[value_slot(combo)];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError>
  policy_canonical_decision(const CanonicalPublicNode &canonical,
                            const std::uint8_t updating_player,
                            const std::array<ComboVector, 2> &reach, const bool best_response) {
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
    for (const ComboId combo : board.legal_combos) {
      strategies[value_slot(combo)] = current_strategy(decision, board.local_index[combo], true);
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      if (canonical.edges[action].outcomes.size() != 1U) {
        return Result<ComboVector, PostflopSolverError>::failure(
            PostflopSolverError::InvalidConfiguration);
      }
      auto child_reach = reach;
      if (!(best_response && decision.player == updating_player)) {
        for (const ComboId combo : board.legal_combos) {
          child_reach[decision.player][value_slot(combo)] *= strategies[value_slot(combo)][action];
        }
      }
      const auto &outcome = canonical.edges[action].outcomes.front();
      child_reach = transform_reach(child_reach, outcome.physical_to_child_automorphism);
      auto child = policy_canonical(outcome.child, updating_player, child_reach, best_response);
      if (!child) {
        return child;
      }
      action_values[action] =
          transform_values_to_parent(child.value(), outcome.physical_to_child_automorphism);
    }
    ComboVector values{};
    for (const ComboId combo : board.legal_combos) {
      const auto slot = value_slot(combo);
      if (best_response && decision.player == updating_player) {
        values[slot] = action_values[0][slot];
        for (std::size_t action = 1; action < action_count; ++action) {
          values[slot] = std::max(values[slot], action_values[action][slot]);
        }
      } else {
        const auto &strategy = strategies[slot];
        if (decision.player == updating_player) {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategy[action] * action_values[action][slot];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
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

  Result<ComboVector, PostflopSolverError> fold_values(const PublicTreeNode &node,
                                                       const std::uint8_t updating_player,
                                                       const ComboVector &opponent_reach) const {
    return fold_values(node.state, layout_.node_board[static_cast<std::size_t>(node.id)],
                       updating_player, opponent_reach);
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
    return fold_values_with_payoff(board_index, payoff, opponent_reach);
  }

  Result<ComboVector, PostflopSolverError>
  fold_values_with_payoff(const std::uint32_t board_index, const double payoff,
                          const ComboVector &opponent_reach) const {
    double total = 0.0;
    std::array<double, 36> by_card{};
    const auto &board = layout_.boards[board_index];
    for (const ComboId combo_id : board.legal_combos) {
      const double weight = opponent_reach[value_slot(combo_id)];
      total += weight;
      by_card[layout_.combos[combo_id].first.value()] += weight;
      by_card[layout_.combos[combo_id].second.value()] += weight;
    }
    ComboVector values{};
    for (const ComboId combo_id : board.legal_combos) {
      const auto &combo = layout_.combos[combo_id];
      const double compatible = total - by_card[combo.first.value()] -
                                by_card[combo.second.value()] +
                                opponent_reach[value_slot(combo_id)];
      values[value_slot(combo_id)] = compatible * payoff / layout_.initial_normalization;
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError> showdown_values(const PublicTreeNode &node,
                                                           const std::uint8_t updating_player,
                                                           const ComboVector &opponent_reach) {
    return showdown_values(node.state, layout_.node_board[static_cast<std::size_t>(node.id)],
                           updating_player, opponent_reach);
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
                                        opponent_reach);
  }

  Result<ComboVector, PostflopSolverError>
  showdown_values_with_payoffs(const std::uint32_t board_index, const double win_payoff,
                               const double tie_payoff, const double loss_payoff,
                               const ComboVector &opponent_reach) {
    const auto prepared = prepare_ranks(layout_, board_index);
    if (!prepared) {
      return Result<ComboVector, PostflopSolverError>::failure(prepared.error());
    }
    const auto &board = layout_.boards[board_index];
    const auto rank_count = static_cast<std::size_t>(board.rank_count);
    const auto calculate = [&](auto &totals, auto &by_card, auto &prefix, auto &card_prefix) {
      for (const ComboId combo_id : board.legal_combos) {
        const auto rank = static_cast<std::size_t>(board.rank_index[combo_id]);
        const double weight = opponent_reach[value_slot(combo_id)];
        totals[rank] += weight;
        by_card[static_cast<std::size_t>(layout_.combos[combo_id].first.value()) * rank_count +
                rank] += weight;
        by_card[static_cast<std::size_t>(layout_.combos[combo_id].second.value()) * rank_count +
                rank] += weight;
      }
      for (std::size_t rank = 0; rank < rank_count; ++rank) {
        prefix[rank + 1U] = prefix[rank] + totals[rank];
        for (std::size_t card = 0; card < 36U; ++card) {
          card_prefix[card * (rank_count + 1U) + rank + 1U] =
              card_prefix[card * (rank_count + 1U) + rank] + by_card[card * rank_count + rank];
        }
      }
      ComboVector values{};
      for (const ComboId combo_id : board.legal_combos) {
        const auto rank = static_cast<std::size_t>(board.rank_index[combo_id]);
        const auto first = static_cast<std::size_t>(layout_.combos[combo_id].first.value());
        const auto second = static_cast<std::size_t>(layout_.combos[combo_id].second.value());
        const double invalid_lower = card_prefix[first * (rank_count + 1U) + rank] +
                                     card_prefix[second * (rank_count + 1U) + rank];
        const double invalid_tie = by_card[first * rank_count + rank] +
                                   by_card[second * rank_count + rank] -
                                   opponent_reach[value_slot(combo_id)];
        const double invalid_all = card_prefix[first * (rank_count + 1U) + rank_count] +
                                   card_prefix[second * (rank_count + 1U) + rank_count] -
                                   opponent_reach[value_slot(combo_id)];
        const double lower = prefix[rank] - invalid_lower;
        const double equal = totals[rank] - invalid_tie;
        const double higher =
            (prefix[rank_count] - prefix[rank + 1U]) - (invalid_all - invalid_lower - invalid_tie);
        values[value_slot(combo_id)] =
            (lower * win_payoff + equal * tie_payoff + higher * loss_payoff) /
            layout_.initial_normalization;
      }
      return values;
    };
    if constexpr (Capacity <= compact_combo_capacity) {
      std::fill_n(showdown_totals_.begin(), rank_count, 0.0);
      std::fill_n(showdown_by_card_.begin(), 36U * rank_count, 0.0);
      std::fill_n(showdown_prefix_.begin(), rank_count + 1U, 0.0);
      std::fill_n(showdown_card_prefix_.begin(), 36U * (rank_count + 1U), 0.0);
      return Result<ComboVector, PostflopSolverError>::success(
          calculate(showdown_totals_, showdown_by_card_, showdown_prefix_, showdown_card_prefix_));
    } else {
      std::vector<double> totals(rank_count, 0.0);
      std::vector<double> by_card(36U * rank_count, 0.0);
      std::vector<double> prefix(rank_count + 1U, 0.0);
      std::vector<double> card_prefix(36U * (rank_count + 1U), 0.0);
      return Result<ComboVector, PostflopSolverError>::success(
          calculate(totals, by_card, prefix, card_prefix));
    }
  }

  std::array<ComboVector, 2> block_card(const std::array<ComboVector, 2> &reach,
                                        const BoardData &board, const CardId card) const {
    auto blocked = reach;
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

  Result<ComboVector, PostflopSolverError> cfr_chance(const PublicTreeNode &node,
                                                      const std::uint8_t updating_player,
                                                      const std::array<ComboVector, 2> &reach,
                                                      const double strategy_weight) {
    ComboVector values{};
    if (node.edges.empty() || node.edges.front().total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    for (const auto &edge : node.edges) {
      auto child_reach = block_card(reach, board, edge.chance_card);
      const auto child = cfr_physical(edge.child, updating_player, child_reach, strategy_weight);
      if (!child) {
        return child;
      }
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      for (const ComboId combo_id : board.legal_combos) {
        if ((layout_.combo_masks[combo_id] & edge.chance_card.mask()) == 0U) {
          values[value_slot(combo_id)] += probability * child.value()[value_slot(combo_id)];
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError> cfr_decision(const PublicTreeNode &node,
                                                        const std::uint8_t updating_player,
                                                        const std::array<ComboVector, 2> &reach,
                                                        const double strategy_weight) {
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    std::array<ComboVector, maximum_action_count> action_values{};
    std::array<std::array<double, maximum_action_count>, combo_count> strategies{};
    for (const ComboId combo_id : board.legal_combos) {
      strategies[combo_id] = current_strategy(decision, board.local_index[combo_id], false);
    }

    for (std::size_t action = 0; action < action_count; ++action) {
      auto child_reach = reach;
      for (const ComboId combo_id : board.legal_combos) {
        child_reach[decision.player][value_slot(combo_id)] *= strategies[combo_id][action];
      }
      const auto child =
          cfr_physical(node.edges[action].child, updating_player, child_reach, strategy_weight);
      if (!child) {
        return child;
      }
      action_values[action] = child.value();
    }

    ComboVector values{};
    for (const ComboId combo_id : board.legal_combos) {
      const auto slot = value_slot(combo_id);
      const auto local = board.local_index[combo_id];
      const auto &strategy = strategies[combo_id];
      const auto offset = decision_action_base(layout_, decision, local);
      if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += strategy[action] * action_values[action][slot];
        }
      } else {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[slot] += action_values[action][slot];
        }
      }
      if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto index = static_cast<std::size_t>(offset + action);
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
          add_strategy(index, strategy_weight * reach[updating_player][slot] * strategy[action]);
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError> policy_chance(const PublicTreeNode &node,
                                                         const std::uint8_t updating_player,
                                                         const std::array<ComboVector, 2> &reach,
                                                         const bool best_response) {
    ComboVector values{};
    if (node.edges.empty() || node.edges.front().total_legal_outcome_count <= 4U) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    for (const auto &edge : node.edges) {
      auto child_reach = block_card(reach, board, edge.chance_card);
      const auto child = policy_physical(edge.child, updating_player, child_reach, best_response);
      if (!child) {
        return child;
      }
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      for (const ComboId combo_id : board.legal_combos) {
        if ((layout_.combo_masks[combo_id] & edge.chance_card.mask()) == 0U) {
          values[value_slot(combo_id)] += probability * child.value()[value_slot(combo_id)];
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError> policy_decision(const PublicTreeNode &node,
                                                           const std::uint8_t updating_player,
                                                           const std::array<ComboVector, 2> &reach,
                                                           const bool best_response) {
    const auto &decision = layout_.decisions[static_cast<std::size_t>(node.id)];
    const auto &board = layout_.boards[decision.board_index];
    const auto action_count = static_cast<std::size_t>(decision.action_count);
    std::array<ComboVector, maximum_action_count> action_values{};
    std::array<std::array<double, maximum_action_count>, combo_count> strategies{};
    for (const ComboId combo_id : board.legal_combos) {
      strategies[combo_id] = current_strategy(decision, board.local_index[combo_id], true);
    }
    for (std::size_t action = 0; action < action_count; ++action) {
      auto child_reach = reach;
      if (!(best_response && decision.player == updating_player)) {
        for (const ComboId combo_id : board.legal_combos) {
          child_reach[decision.player][value_slot(combo_id)] *= strategies[combo_id][action];
        }
      }
      const auto child =
          policy_physical(node.edges[action].child, updating_player, child_reach, best_response);
      if (!child) {
        return child;
      }
      action_values[action] = child.value();
    }
    ComboVector values{};
    for (const ComboId combo_id : board.legal_combos) {
      const auto slot = value_slot(combo_id);
      if (best_response && decision.player == updating_player) {
        values[slot] = action_values[0][slot];
        for (std::size_t action = 1; action < action_count; ++action) {
          values[slot] = std::max(values[slot], action_values[action][slot]);
        }
      } else {
        const auto &strategy = strategies[combo_id];
        if (decision.player == updating_player) {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += strategy[action] * action_values[action][slot];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[slot] += action_values[action][slot];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  DenseLayout &layout_;
  ActionBuffers buffers_;
  std::vector<double> *deferred_regret_delta_{nullptr};
  std::vector<std::size_t> deferred_regret_touched_;
  std::vector<std::uint8_t> deferred_regret_touched_flags_;
  std::vector<double> parallel_regret_delta_;
  std::unique_ptr<DenseTraversal> parallel_worker_;
  std::mutex parallel_task_mutex_;
  std::condition_variable parallel_task_ready_;
  std::optional<std::packaged_task<TraversalResult()>> parallel_task_;
  bool parallel_shutdown_{false};
  std::jthread parallel_thread_;
  std::vector<std::unique_ptr<DecisionScratch>> decision_scratch_;
  std::array<double, compact_combo_capacity> showdown_totals_{};
  std::array<double, 36U * compact_combo_capacity> showdown_by_card_{};
  std::array<double, compact_combo_capacity + 1U> showdown_prefix_{};
  std::array<double, 36U * (compact_combo_capacity + 1U)> showdown_card_prefix_{};
  std::size_t decision_scratch_depth_{0};
  std::uint64_t traversed_nodes_{0};
  double maximum_normalization_error_{0.0};
};

template <std::size_t Capacity>
std::array<TraversalComboVector<Capacity>, 2> initial_reach(const DenseLayout &layout) {
  std::array<TraversalComboVector<Capacity>, 2> reach{};
  for (const ComboId combo : layout.active_combos) {
    const auto slot = Capacity == combo_count
                          ? static_cast<std::size_t>(combo)
                          : static_cast<std::size_t>(layout.active_combo_index[combo]);
    reach[0][slot] = layout.initial_reach[0][combo];
    reach[1][slot] = layout.initial_reach[1][combo];
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

template <std::size_t Capacity>
class TypedDenseTraversalRunner final : public DenseTraversalRunner {
public:
  TypedDenseTraversalRunner(DenseLayout &layout, const ActionBuffers buffers,
                            std::vector<double> *deferred_regret_delta,
                            const std::uint8_t parallel_action_depth)
      : traversal_(layout, buffers, deferred_regret_delta, parallel_action_depth),
        reach_(initial_reach<Capacity>(layout)), root_(layout.tree.root) {}

  Result<bool, PostflopSolverError> cfr(const std::uint8_t updating_player,
                                        const double strategy_weight) override {
    const auto traversed = traversal_.cfr(root_, updating_player, reach_, strategy_weight);
    if (!traversed) {
      return Result<bool, PostflopSolverError>::failure(traversed.error());
    }
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
  DenseTraversal<Capacity> traversal_;
  std::array<TraversalComboVector<Capacity>, 2> reach_;
  NodeId root_{0};
};

std::unique_ptr<DenseTraversalRunner>
make_dense_traversal_runner(DenseLayout &layout, const ActionBuffers buffers,
                            std::vector<double> *deferred_regret_delta,
                            const std::uint8_t parallel_action_depth) {
  if (layout.uses_canonical_public_dag && layout.active_combos.size() <= compact_combo_capacity) {
    return std::make_unique<TypedDenseTraversalRunner<compact_combo_capacity>>(
        layout, buffers, deferred_regret_delta, parallel_action_depth);
  }
  return std::make_unique<TypedDenseTraversalRunner<combo_count>>(
      layout, buffers, deferred_regret_delta, parallel_action_depth);
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

template <std::size_t Capacity>
Result<PostflopCertification, PostflopSolverError>
certify_typed(DenseLayout &layout, const ActionBuffers buffers, const std::uint64_t iteration) {
  const auto reach = initial_reach<Capacity>(layout);
  for (std::uint32_t board_index = 0; board_index < layout.boards.size(); ++board_index) {
    if (std::popcount(layout.boards[board_index].mask) == 5) {
      const auto prepared = prepare_ranks(layout, board_index);
      if (!prepared) {
        return Result<PostflopCertification, PostflopSolverError>::failure(prepared.error());
      }
    }
  }
  if (!layout.uses_canonical_public_dag) {
    DenseTraversal<Capacity> traversal(layout, buffers);
    PostflopCertification certification;
    certification.iteration = iteration;
    for (std::uint8_t player = 0; player < 2U; ++player) {
      const auto profile = traversal.policy(layout.tree.root, player, reach, false);
      const auto response = traversal.policy(layout.tree.root, player, reach, true);
      if (!profile || !response) {
        return Result<PostflopCertification, PostflopSolverError>::failure(
            profile ? response.error() : profile.error());
      }
      certification.profile_value_antes[player] =
          std::accumulate(profile.value().begin(), profile.value().end(), 0.0);
      certification.best_response_value_antes[player] =
          std::accumulate(response.value().begin(), response.value().end(), 0.0);
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
  const auto evaluate = [&layout, buffers, &reach](const std::uint8_t player,
                                                   const bool best_response) {
    DenseTraversal<Capacity> traversal(layout, buffers);
    const auto values = traversal.policy(layout.tree.root, player, reach, best_response);
    if (!values) {
      return Result<double, PostflopSolverError>::failure(values.error());
    }
    return Result<double, PostflopSolverError>::success(
        std::accumulate(values.value().begin(), values.value().end(), 0.0));
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
certify(DenseLayout &layout, const ActionBuffers buffers, const std::uint64_t iteration) {
  if (layout.uses_canonical_public_dag && layout.active_combos.size() <= compact_combo_capacity) {
    return certify_typed<compact_combo_capacity>(layout, buffers, iteration);
  }
  return certify_typed<combo_count>(layout, buffers, iteration);
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
  if (layout.value().uses_isomorphic_infosets) {
    deferred_regret_delta.resize(static_cast<std::size_t>(layout.value().actions), 0.0);
  }
  auto traversal = make_dense_traversal_runner(
      layout.value(), buffers,
      layout.value().uses_isomorphic_infosets ? &deferred_regret_delta : nullptr,
      options.parallel_action_depth);
  result.timings.layout_seconds = 0.0;
  result.timings.initialization_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - initialization_started)
          .count();
  if (checkpoint.completed_iterations == options.iterations) {
    const auto certification_started = std::chrono::steady_clock::now();
    const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations);
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
      const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations);
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
    return certify(layout.value(), mapped.buffers(), checkpoint.completed_iterations);
  }
  const auto buffers = in_memory_checkpoint_buffers(checkpoint);
  if (!buffers) {
    return Result<PostflopCertification, PostflopSolverError>::failure(buffers.error());
  }
  return certify(layout.value(), buffers.value(), checkpoint.completed_iterations);
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
    const auto values = traversal.policy_from_physical_node(public_node, player, reach);
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
