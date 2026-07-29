#include "gtosd/postflop/postflop_solver.hpp"

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string_view>
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
constexpr double units_per_ante = 10'000.0;
using ComboVector = std::array<double, combo_count>;

class PagedActionFile;

struct ActionBuffers {
  double *regret{nullptr};
  double *strategy{nullptr};
  std::size_t count{0};
  PagedActionFile *paged{nullptr};

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
  return paged != nullptr ? paged->get(index) : regret[index];
}

double ActionBuffers::strategy_at(const std::size_t index) const {
  return paged != nullptr ? paged->get(count + index) : strategy[index];
}

void ActionBuffers::set_regret(const std::size_t index, const double value) const {
  if (paged != nullptr) {
    paged->set(index, value);
  } else {
    regret[index] = value;
  }
}

void ActionBuffers::add_strategy(const std::size_t index, const double value) const {
  if (paged != nullptr) {
    paged->set(count + index, paged->get(count + index) + value);
  } else {
    strategy[index] += value;
  }
}

struct BoardData {
  std::uint64_t mask{0};
  std::array<std::int16_t, combo_count> local_index{};
  std::vector<ComboId> legal_combos;
  std::array<std::int16_t, combo_count> rank_index{};
  std::uint16_t rank_count{0};
  bool ranks_ready{false};
};

struct DecisionLayout {
  std::uint64_t action_base{0};
  std::uint32_t board_index{0};
  std::uint16_t action_count{0};
  std::uint8_t player{0};
  bool present{false};
};

struct DenseLayout {
  PublicTree tree;
  std::array<Combo, combo_count> combos{};
  std::array<std::uint64_t, combo_count> combo_masks{};
  std::array<ComboVector, 2> initial_reach{};
  std::vector<BoardData> boards;
  std::vector<std::uint32_t> node_board;
  std::vector<DecisionLayout> decisions;
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  double initial_normalization{0.0};
  std::string fingerprint;
};

bool finite_vector(const std::vector<double> &values) {
  return std::all_of(values.begin(), values.end(),
                     [](const double value) { return std::isfinite(value); });
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

Result<DenseLayout, PostflopSolverError> build_layout(const PostflopTreeConfig &config,
                                                      const PostflopRanges &ranges) {
  TreeBuildOptions options;
  options.maximum_nodes = std::numeric_limits<std::uint64_t>::max();
  auto tree = build_public_tree(config, options);
  if (!tree) {
    return Result<DenseLayout, PostflopSolverError>::failure(PostflopSolverError::TreeFailure);
  }

  DenseLayout layout;
  layout.tree = std::move(tree.value());
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
        if ((layout.combo_masks[combo] & board.mask) == 0U) {
          board.local_index[combo] = static_cast<std::int16_t>(board.legal_combos.size());
          board.legal_combos.push_back(static_cast<ComboId>(combo));
        }
      }
      const auto board_index = static_cast<std::uint32_t>(layout.boards.size());
      layout.boards.push_back(std::move(board));
      found = board_lookup.emplace(node.state.board_mask, board_index).first;
    }
    layout.node_board[static_cast<std::size_t>(node.id)] = found->second;
    if (node.kind != PublicNodeKind::Decision) {
      continue;
    }
    const auto action_count = node.edges.size();
    const auto legal_count = layout.boards[found->second].legal_combos.size();
    if (action_count == 0U || action_count > maximum_action_count ||
        legal_count > (std::numeric_limits<std::uint64_t>::max() - layout.actions) / action_count) {
      return Result<DenseLayout, PostflopSolverError>::failure(
          PostflopSolverError::InvalidConfiguration);
    }
    auto &decision = layout.decisions[static_cast<std::size_t>(node.id)];
    decision.action_base = layout.actions;
    decision.board_index = found->second;
    decision.action_count = static_cast<std::uint16_t>(action_count);
    decision.player = node.state.player_to_act;
    decision.present = true;
    layout.information_sets += legal_count;
    layout.actions += legal_count * action_count;
  }
  if (layout.actions > std::numeric_limits<std::size_t>::max()) {
    return Result<DenseLayout, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }

  const auto &flop_board = layout.boards[layout.node_board[layout.tree.root]];
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
    fingerprint_source += "|ranges-v1|" + serialize_range_fingerprint(ranges);
  }
  layout.fingerprint = fingerprint_text(fingerprint_source);
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

class DenseTraversal {
public:
  DenseTraversal(DenseLayout &layout, const ActionBuffers buffers)
      : layout_(layout), buffers_(buffers) {}

  Result<ComboVector, PostflopSolverError> cfr(const NodeId node_id,
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

  Result<ComboVector, PostflopSolverError> policy(const NodeId node_id,
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

  [[nodiscard]] std::uint64_t traversed_nodes() const noexcept { return traversed_nodes_; }
  [[nodiscard]] double maximum_normalization_error() const noexcept {
    return maximum_normalization_error_;
  }

private:
  std::array<double, maximum_action_count> current_strategy(const DecisionLayout &decision,
                                                            const std::int16_t local_combo,
                                                            const bool average) {
    const auto count = static_cast<std::size_t>(decision.action_count);
    const auto offset =
        decision.action_base + static_cast<std::uint64_t>(local_combo) * decision.action_count;
    std::array<double, maximum_action_count> strategy{};
    double sum = 0.0;
    for (std::size_t action = 0; action < count; ++action) {
      const double source =
          average ? buffers_.strategy_at(static_cast<std::size_t>(offset + action))
                  : std::max(0.0, buffers_.regret_at(static_cast<std::size_t>(offset + action)));
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
    const double normalized_sum = std::accumulate(
        strategy.begin(), strategy.begin() + static_cast<std::ptrdiff_t>(count), 0.0);
    if (std::isfinite(normalized_sum)) {
      maximum_normalization_error_ =
          std::max(maximum_normalization_error_, std::abs(normalized_sum - 1.0));
    }
    return strategy;
  }

  Result<ComboVector, PostflopSolverError> fold_values(const PublicTreeNode &node,
                                                       const std::uint8_t updating_player,
                                                       const ComboVector &opponent_reach) const {
    const auto settlement = settle_terminal(node.state, layout_.tree.config.rake);
    if (!settlement) {
      return Result<ComboVector, PostflopSolverError>::failure(
          PostflopSolverError::SettlementFailure);
    }
    const double payoff =
        static_cast<double>(settlement.value().payoff_units[updating_player]) / units_per_ante;
    double total = 0.0;
    std::array<double, 36> by_card{};
    for (std::size_t combo = 0; combo < combo_count; ++combo) {
      const double weight = opponent_reach[combo];
      total += weight;
      by_card[layout_.combos[combo].first.value()] += weight;
      by_card[layout_.combos[combo].second.value()] += weight;
    }
    ComboVector values{};
    const auto &board = layout_.boards[layout_.node_board[static_cast<std::size_t>(node.id)]];
    for (const ComboId combo_id : board.legal_combos) {
      const auto &combo = layout_.combos[combo_id];
      const double compatible = total - by_card[combo.first.value()] -
                                by_card[combo.second.value()] + opponent_reach[combo_id];
      values[combo_id] = compatible * payoff / layout_.initial_normalization;
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  Result<ComboVector, PostflopSolverError> showdown_values(const PublicTreeNode &node,
                                                           const std::uint8_t updating_player,
                                                           const ComboVector &opponent_reach) {
    const auto board_index = layout_.node_board[static_cast<std::size_t>(node.id)];
    const auto prepared = prepare_ranks(layout_, board_index);
    if (!prepared) {
      return Result<ComboVector, PostflopSolverError>::failure(prepared.error());
    }
    const auto &board = layout_.boards[board_index];
    const auto own_win = settle_terminal(node.state, layout_.tree.config.rake,
                                         static_cast<std::uint8_t>(1U << updating_player));
    const auto tie = settle_terminal(node.state, layout_.tree.config.rake, 0b11U);
    const auto own_loss = settle_terminal(node.state, layout_.tree.config.rake,
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

    const auto rank_count = static_cast<std::size_t>(board.rank_count);
    std::vector<double> totals(rank_count, 0.0);
    std::vector<double> by_card(36U * rank_count, 0.0);
    for (const ComboId combo_id : board.legal_combos) {
      const auto rank = static_cast<std::size_t>(board.rank_index[combo_id]);
      const double weight = opponent_reach[combo_id];
      totals[rank] += weight;
      by_card[static_cast<std::size_t>(layout_.combos[combo_id].first.value()) * rank_count +
              rank] += weight;
      by_card[static_cast<std::size_t>(layout_.combos[combo_id].second.value()) * rank_count +
              rank] += weight;
    }
    std::vector<double> prefix(rank_count + 1U, 0.0);
    std::vector<double> card_prefix(36U * (rank_count + 1U), 0.0);
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
                                 by_card[second * rank_count + rank] - opponent_reach[combo_id];
      const double invalid_all = card_prefix[first * (rank_count + 1U) + rank_count] +
                                 card_prefix[second * (rank_count + 1U) + rank_count] -
                                 opponent_reach[combo_id];
      const double lower = prefix[rank] - invalid_lower;
      const double equal = totals[rank] - invalid_tie;
      const double higher =
          (prefix[rank_count] - prefix[rank + 1U]) - (invalid_all - invalid_lower - invalid_tie);
      values[combo_id] = (lower * win_payoff + equal * tie_payoff + higher * loss_payoff) /
                         layout_.initial_normalization;
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  std::array<ComboVector, 2> block_card(const std::array<ComboVector, 2> &reach,
                                        const CardId card) const {
    auto blocked = reach;
    for (std::size_t combo = 0; combo < combo_count; ++combo) {
      if ((layout_.combo_masks[combo] & card.mask()) != 0U) {
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
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    for (const auto &edge : node.edges) {
      auto child_reach = block_card(reach, edge.chance_card);
      const auto child = cfr(edge.child, updating_player, child_reach, strategy_weight);
      if (!child) {
        return child;
      }
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      for (std::size_t combo = 0; combo < combo_count; ++combo) {
        if ((layout_.combo_masks[combo] & edge.chance_card.mask()) == 0U) {
          values[combo] += probability * child.value()[combo];
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
        child_reach[decision.player][combo_id] *= strategies[combo_id][action];
      }
      const auto child =
          cfr(node.edges[action].child, updating_player, child_reach, strategy_weight);
      if (!child) {
        return child;
      }
      action_values[action] = child.value();
    }

    ComboVector values{};
    for (const ComboId combo_id : board.legal_combos) {
      const auto local = board.local_index[combo_id];
      const auto &strategy = strategies[combo_id];
      const auto offset =
          decision.action_base + static_cast<std::uint64_t>(local) * decision.action_count;
      if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[combo_id] += strategy[action] * action_values[action][combo_id];
        }
      } else {
        for (std::size_t action = 0; action < action_count; ++action) {
          values[combo_id] += action_values[action][combo_id];
        }
      }
      if (decision.player == updating_player) {
        for (std::size_t action = 0; action < action_count; ++action) {
          const auto index = static_cast<std::size_t>(offset + action);
          buffers_.set_regret(index, std::max(0.0, buffers_.regret_at(index) +
                                                       action_values[action][combo_id] -
                                                       values[combo_id]));
          buffers_.add_strategy(index, strategy_weight * reach[updating_player][combo_id] *
                                           strategy[action]);
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
    const double denominator =
        static_cast<double>(node.edges.front().total_legal_outcome_count - 4U);
    for (const auto &edge : node.edges) {
      auto child_reach = block_card(reach, edge.chance_card);
      const auto child = policy(edge.child, updating_player, child_reach, best_response);
      if (!child) {
        return child;
      }
      const double probability = static_cast<double>(edge.physical_outcome_count) / denominator;
      for (std::size_t combo = 0; combo < combo_count; ++combo) {
        if ((layout_.combo_masks[combo] & edge.chance_card.mask()) == 0U) {
          values[combo] += probability * child.value()[combo];
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
          child_reach[decision.player][combo_id] *= strategies[combo_id][action];
        }
      }
      const auto child =
          policy(node.edges[action].child, updating_player, child_reach, best_response);
      if (!child) {
        return child;
      }
      action_values[action] = child.value();
    }
    ComboVector values{};
    for (const ComboId combo_id : board.legal_combos) {
      if (best_response && decision.player == updating_player) {
        values[combo_id] = action_values[0][combo_id];
        for (std::size_t action = 1; action < action_count; ++action) {
          values[combo_id] = std::max(values[combo_id], action_values[action][combo_id]);
        }
      } else {
        const auto &strategy = strategies[combo_id];
        if (decision.player == updating_player) {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[combo_id] += strategy[action] * action_values[action][combo_id];
          }
        } else {
          for (std::size_t action = 0; action < action_count; ++action) {
            values[combo_id] += action_values[action][combo_id];
          }
        }
      }
    }
    return Result<ComboVector, PostflopSolverError>::success(values);
  }

  DenseLayout &layout_;
  ActionBuffers buffers_;
  std::uint64_t traversed_nodes_{0};
  double maximum_normalization_error_{0.0};
};

std::array<ComboVector, 2> initial_reach(const DenseLayout &layout) { return layout.initial_reach; }

Result<PostflopCertification, PostflopSolverError>
certify(DenseLayout &layout, const ActionBuffers buffers, const std::uint64_t iteration) {
  const auto reach = initial_reach(layout);
  DenseTraversal traversal(layout, buffers);
  PostflopCertification certification;
  certification.iteration = iteration;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    const auto profile = traversal.policy(layout.tree.root, player, reach, false);
    const auto response = traversal.policy(layout.tree.root, player, reach, true);
    if (!profile || !response) {
      return Result<PostflopCertification, PostflopSolverError>::failure(profile ? response.error()
                                                                                 : profile.error());
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
  const auto offset =
      decision.action_base + static_cast<std::uint64_t>(local) * decision.action_count;
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

} // namespace

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
  if (options.iterations == 0U || options.certification_interval == 0U ||
      options.memory_backend == MemoryPrototype::StreetDecomposition) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  if (!validate_postflop_ranges(config, ranges)) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::InvalidConfiguration);
  }
  auto layout = build_layout(config, ranges);
  if (!layout) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(layout.error());
  }
  PostflopCheckpoint checkpoint;
  std::unique_ptr<PagedActionFile> mapped;
  ActionBuffers buffers;
  const bool out_of_core = options.memory_backend == MemoryPrototype::OutOfCore;
  if (resume_from != nullptr) {
    if (resume_from->game_fingerprint != layout.value().fingerprint ||
        resume_from->averaging_delay != options.averaging_delay ||
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
    buffers = mapped->buffers();
  } else {
    if (resume_from != nullptr &&
        (checkpoint.cumulative_regret.size() != layout.value().actions ||
         checkpoint.cumulative_strategy.size() != layout.value().actions ||
         !checkpoint.external_buffer_file.empty())) {
      return Result<PostflopSolveResult, PostflopSolverError>::failure(
          PostflopSolverError::CheckpointMismatch);
    }
    if (resume_from == nullptr) {
      checkpoint.cumulative_regret.resize(static_cast<std::size_t>(layout.value().actions), 0.0);
      checkpoint.cumulative_strategy.resize(static_cast<std::size_t>(layout.value().actions), 0.0);
    }
    buffers = {checkpoint.cumulative_regret.data(), checkpoint.cumulative_strategy.data(),
               checkpoint.cumulative_regret.size()};
  }

  PostflopSolveResult result;
  result.public_tree = layout.value().tree.stats;
  result.information_sets = layout.value().information_sets;
  result.actions = layout.value().actions;
  DenseTraversal traversal(layout.value(), buffers);
  const auto reach = initial_reach(layout.value());
  if (checkpoint.completed_iterations == options.iterations) {
    const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations);
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
    const double strategy_weight = iteration > options.averaging_delay
                                       ? static_cast<double>(iteration - options.averaging_delay)
                                       : 0.0;
    for (std::uint8_t player = 0; player < 2U; ++player) {
      const auto traversed =
          traversal.cfr(layout.value().tree.root, player, reach, strategy_weight);
      if (!traversed) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(traversed.error());
      }
    }
    checkpoint.completed_iterations = iteration;
    const auto control = options.control_callback ? options.control_callback(iteration)
                                                  : PostflopControlCommand::Continue;
    const bool stopping = control != PostflopControlCommand::Continue;
    if (iteration % options.certification_interval == 0U || iteration == options.iterations ||
        stopping) {
      const auto certification = certify(layout.value(), buffers, checkpoint.completed_iterations);
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
    if (stopping) {
      result.stop_reason = control == PostflopControlCommand::Pause ? PostflopStopReason::Paused
                                                                    : PostflopStopReason::Cancelled;
      break;
    }
  }
  if (out_of_core) {
    for (std::size_t index = 0; index < buffers.count; ++index) {
      if (!std::isfinite(buffers.regret_at(index)) || !std::isfinite(buffers.strategy_at(index))) {
        return Result<PostflopSolveResult, PostflopSolverError>::failure(
            PostflopSolverError::NumericalFailure);
      }
    }
  } else if (!finite_vector(checkpoint.cumulative_regret) ||
             !finite_vector(checkpoint.cumulative_strategy)) {
    return Result<PostflopSolveResult, PostflopSolverError>::failure(
        PostflopSolverError::NumericalFailure);
  }
  result.traversed_nodes = traversal.traversed_nodes();
  result.maximum_normalization_error = traversal.maximum_normalization_error();
  result.checkpoint = std::move(checkpoint);
  return Result<PostflopSolveResult, PostflopSolverError>::success(std::move(result));
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
  if (checkpoint.cumulative_regret.size() != layout.value().actions ||
      checkpoint.cumulative_strategy.size() != layout.value().actions) {
    return Result<PostflopCertification, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
  }
  ActionBuffers buffers{const_cast<double *>(checkpoint.cumulative_regret.data()),
                        const_cast<double *>(checkpoint.cumulative_strategy.data()),
                        checkpoint.cumulative_regret.size()};
  return certify(layout.value(), buffers, checkpoint.completed_iterations);
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
  ActionBuffers query_buffers{nullptr, const_cast<double *>(checkpoint.cumulative_strategy.data()),
                              checkpoint.cumulative_strategy.size(), nullptr};
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    query_buffers = mapped->buffers();
  } else if (checkpoint.cumulative_strategy.size() != layout.value().actions) {
    return Result<PostflopStrategyQuery, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
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
  ActionBuffers query_buffers{nullptr, const_cast<double *>(checkpoint.cumulative_strategy.data()),
                              checkpoint.cumulative_strategy.size(), nullptr};
  if (!checkpoint.external_buffer_file.empty()) {
    mapped = std::make_unique<PagedActionFile>(
        checkpoint.external_buffer_file, static_cast<std::size_t>(checkpoint.action_count), false);
    if (!mapped->valid()) {
      return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
          PostflopSolverError::IoFailure);
    }
    query_buffers = mapped->buffers();
  } else if (checkpoint.cumulative_strategy.size() != layout.value().actions) {
    return Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>::failure(
        PostflopSolverError::CheckpointMismatch);
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
