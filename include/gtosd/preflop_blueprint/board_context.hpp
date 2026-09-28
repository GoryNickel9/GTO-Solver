#pragma once

#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/core/cards.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/core/result.hpp"
#include "gtosd/preflop_blueprint/board_class_rows.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Per-board tables of the vector kernels.
//
// A traversal fixes a complete five-card board history (public chance
// sampling). The 465 hole-card combos disjoint from it are the live hands of
// both players; the context stores, once per board, their combo ids, cards,
// seven-card ranks, rank order, preflop hand class, street buckets and the
// per-card incidence lists used by the blocker corrections of the kernels.
namespace gtosd::preflop_blueprint {
class HistoryBucketRows;

inline constexpr std::size_t live_hand_count = 465U;
// Live hands that contain a given non-board card.
inline constexpr std::size_t hands_per_card = 30U;
inline constexpr std::uint16_t no_hand = 0xFFFFU;

enum class KernelError : std::uint8_t { InvalidBoard, MissingTable, InvalidInput };

// Immutable reconstruction of the historical `class` experiment: enumerate
// every occurring (preflop class, current bucket) pair, then number pairs in
// class-major order. No training data or sampled board corpus enters the map.
// A caller importing an old experimental policy must also verify its reference
// certificate: the old policy format does not identify the source bucket tables.
class ClassBucketRows {
public:
  [[nodiscard]] static Result<ClassBucketRows, KernelError>
  build(const card_abstraction::BucketTable &flop, const card_abstraction::BucketTable &turn,
        const card_abstraction::BucketTable &river);
  [[nodiscard]] std::uint16_t row(card_abstraction::BucketStreet street, std::uint8_t hand_class,
                                  std::uint16_t bucket) const noexcept;
  [[nodiscard]] std::uint32_t count(card_abstraction::BucketStreet street) const noexcept {
    return counts_[static_cast<std::size_t>(street)];
  }
  [[nodiscard]] bool matches(const card_abstraction::BucketTable &table) const noexcept;

private:
  std::array<std::vector<std::uint16_t>, 3> rows_;
  std::array<std::uint16_t, 3> capacities_{};
  std::array<std::uint32_t, 3> counts_{};
  std::array<std::string, 3> fingerprints_;
};

// Optional street abstraction. When a table is absent the corresponding
// bucket rows hold card_abstraction::no_bucket.
struct AbstractionTables {
  const card_abstraction::BoardCatalog *catalog{nullptr};
  const card_abstraction::BucketTable *flop{nullptr};
  const card_abstraction::BucketTable *turn{nullptr};
  const card_abstraction::BucketTable *river{nullptr};
  // Must outlive contexts built from these tables. Null retains plain buckets.
  const ClassBucketRows *class_rows{nullptr};
  const HistoryBucketRows *history_rows{nullptr};
  // MonkerSolver-style rows keyed by (board class, per-board bucket).
  const BoardClassRows *board_class_rows{nullptr};
};

class BoardContext {
public:
  [[nodiscard]] static Result<BoardContext, KernelError>
  build(const card_abstraction::BoardHistory &history, const card_abstraction::RankTable &ranks,
        const AbstractionTables *tables = nullptr);

  [[nodiscard]] const card_abstraction::BoardHistory &history() const noexcept { return history_; }
  [[nodiscard]] const std::array<CardId, 5> &board() const noexcept { return board_; }
  [[nodiscard]] std::uint64_t board_mask() const noexcept { return board_mask_; }

  // Live hands in increasing combo-id order.
  [[nodiscard]] std::span<const std::uint16_t, live_hand_count> combo_ids() const noexcept {
    return combo_ids_;
  }
  [[nodiscard]] std::span<const std::array<std::uint8_t, 2>, live_hand_count>
  cards() const noexcept {
    return cards_;
  }
  // Index of a combo among the live hands, no_hand when it overlaps the board.
  [[nodiscard]] std::uint16_t hand_index(const std::uint16_t combo) const noexcept {
    return hand_index_[combo];
  }
  [[nodiscard]] std::span<const std::uint16_t, live_hand_count> ranks() const noexcept {
    return ranks_;
  }
  // Hand indices sorted by increasing rank; equal ranks are adjacent.
  [[nodiscard]] std::span<const std::uint16_t, live_hand_count> order_by_rank() const noexcept {
    return order_;
  }
  [[nodiscard]] std::span<const std::uint8_t, live_hand_count> hand_classes() const noexcept {
    return hand_class_;
  }
  // Information row of a hand at a street: the preflop class or the bucket.
  [[nodiscard]] std::uint32_t row(const Street street, const std::uint16_t hand) const noexcept {
    if (street == Street::Preflop) {
      return hand_class_[hand];
    }
    const auto index = static_cast<std::size_t>(street) - 1U;
    if (has_history_rows_)
      return history_rows_[index][hand];
    return class_rows_ == nullptr
               ? buckets_[index][hand]
               : class_rows_->row(static_cast<card_abstraction::BucketStreet>(index),
                                  hand_class_[hand], buckets_[index][hand]);
  }
  [[nodiscard]] std::span<const std::uint16_t, live_hand_count>
  buckets(const Street street) const noexcept {
    return buckets_[static_cast<std::size_t>(street) - 1U];
  }
  [[nodiscard]] bool has_buckets(const Street street) const noexcept {
    return has_buckets_[static_cast<std::size_t>(street) - 1U];
  }
  // Live hands containing `card`; empty for board cards.
  [[nodiscard]] std::span<const std::uint16_t> hands_with_card(const std::uint8_t card) const noexcept {
    return std::span<const std::uint16_t>(hands_with_card_[card].data(),
                                          card_is_live_[card] ? hands_per_card : 0U);
  }
  [[nodiscard]] bool card_is_live(const std::uint8_t card) const noexcept {
    return card_is_live_[card];
  }
  [[nodiscard]] std::uint16_t distinct_rank_groups() const noexcept { return rank_groups_; }

private:
  bool has_history_rows_{false};
  std::array<std::array<std::uint32_t, live_hand_count>, 3> history_rows_{};
  const ClassBucketRows *class_rows_{nullptr};
  card_abstraction::BoardHistory history_{};
  std::array<CardId, 5> board_{};
  std::uint64_t board_mask_{0U};
  std::array<std::uint16_t, live_hand_count> combo_ids_{};
  std::array<std::array<std::uint8_t, 2>, live_hand_count> cards_{};
  std::array<std::uint16_t, 630> hand_index_{};
  std::array<std::uint16_t, live_hand_count> ranks_{};
  std::array<std::uint16_t, live_hand_count> order_{};
  std::array<std::uint8_t, live_hand_count> hand_class_{};
  std::array<std::array<std::uint16_t, live_hand_count>, 3> buckets_{};
  std::array<bool, 3> has_buckets_{};
  std::array<std::array<std::uint16_t, hands_per_card>, 36> hands_with_card_{};
  std::array<bool, 36> card_is_live_{};
  std::uint16_t rank_groups_{0U};
};

[[nodiscard]] const char *kernel_error_name(KernelError error) noexcept;

} // namespace gtosd::preflop_blueprint
