#pragma once

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Joint river engine of the physical best response (best_response.hpp).
//
// The reference path evaluates every turn->river subtree of a board four
// times with ValueTraversal: two heroes, response and average mode. The
// opponent reach at a river node depends only on the opponent's policy, so
// both modes of a hero see the same reach, the same pruning and the same
// terminal masses, and both heroes walk the same nodes. The joint engine
// carries the two heroes as the two lanes of an SSE2 register and the two
// modes as two value vectors, in one pass per subtree and board: one fold or
// showdown sweep per terminal instead of four, one read of every policy row
// instead of up to three. Every floating-point operation of the reference is
// replayed per lane in the same order and association, so the values are
// bit-identical to the reference, signed zeros included.
//
// RiverBoard is the river-only counterpart of BoardContext: the same live
// hands, ranks and rank order, the river rows only, read through a
// RiverPrefix built once per flop-turn prefix instead of once per board.
namespace gtosd::preflop_blueprint {

// Pair layout of the joint engine: hand-major, lane h of hand i at index
// hero_lanes * i + h holds the value of hero h.
inline constexpr std::size_t hero_lanes = 2U;
inline constexpr std::size_t pair_values = hero_lanes * live_hand_count;
using PairSpan = std::span<double, pair_values>;
using ConstPairSpan = std::span<const double, pair_values>;

// Street abstraction of one flop-turn prefix, shared by its rivers.
class RiverPrefix {
public:
  // Checks the tables as BoardContext::build does and reads, for every combo
  // disjoint from the prefix, whether its flop and turn buckets (and class
  // rows) exist and, with board class rows, the class of the turn, which its
  // rivers share. The tables must outlive the prefix and every board assigned
  // from it; all three bucket tables and the catalog are required.
  [[nodiscard]] Result<bool, KernelError> assign(const std::array<CardId, 3> &flop, CardId turn,
                                                 const AbstractionTables &tables);

  [[nodiscard]] bool assigned() const noexcept { return assigned_; }
  [[nodiscard]] const std::array<CardId, 3> &flop() const noexcept { return flop_; }
  [[nodiscard]] CardId turn() const noexcept { return turn_; }
  [[nodiscard]] const AbstractionTables &tables() const noexcept { return tables_; }
  // False when the combo meets the prefix or lacks one of its flop and turn
  // rows: BoardContext::build rejects a board with such a live hand.
  [[nodiscard]] bool covers(const std::uint16_t combo) const noexcept {
    return covered_[combo] != 0U;
  }
  // Board class of the prefix's rivers with board class rows: the texture's
  // river class of the canonical flop+turn index (the index itself without a
  // texture, and without board class rows).
  [[nodiscard]] std::uint32_t turn_class() const noexcept { return turn_class_; }

private:
  std::array<CardId, 3> flop_{};
  CardId turn_{};
  AbstractionTables tables_{};
  bool assigned_{false};
  std::uint32_t turn_class_{0U};
  std::array<std::uint8_t, card_abstraction::combo_count> covered_{};
};

class RiverBoard {
public:
  // Rebuilds the board in place with the live hands, ranks and rank order of
  // BoardContext::build and, given a prefix of the same flop and turn, the
  // river rows of BoardContext::row(Street::River, hand). Together with the
  // RiverPrefix::assign of its flop and turn it accepts exactly the boards
  // that BoardContext::build accepts with the same tables, but a rejection
  // may carry a different error code, since the checks run in another order;
  // evaluate_flop cannot show the difference, as it maps every failure to
  // InvalidInput. Without a prefix every row is no_row, which the joint
  // traversal treats as a missing row.
  [[nodiscard]] Result<bool, KernelError> assign(const card_abstraction::BoardHistory &history,
                                                 const card_abstraction::RankTable &ranks,
                                                 const RiverPrefix *prefix = nullptr);

  [[nodiscard]] const card_abstraction::BoardHistory &history() const noexcept {
    return history_;
  }
  // Live hands in increasing combo-id order.
  [[nodiscard]] std::span<const std::uint16_t, live_hand_count> combo_ids() const noexcept {
    return combo_ids_;
  }
  [[nodiscard]] std::span<const std::array<std::uint8_t, 2>, live_hand_count>
  cards() const noexcept {
    return cards_;
  }
  [[nodiscard]] std::span<const std::uint16_t, live_hand_count> ranks() const noexcept {
    return ranks_;
  }
  // Hands by increasing rank, equal ranks by increasing hand index: the
  // stable order of BoardContext::order_by_rank().
  [[nodiscard]] std::span<const std::uint16_t, live_hand_count> order_by_rank() const noexcept {
    return order_;
  }
  // First position in the rank order of every maximal run of equal rank,
  // followed by live_hand_count.
  [[nodiscard]] std::span<const std::uint16_t> group_starts() const noexcept {
    return std::span<const std::uint16_t>(group_starts_.data(),
                                          static_cast<std::size_t>(rank_groups_) + 1U);
  }
  [[nodiscard]] std::uint16_t rank_groups() const noexcept { return rank_groups_; }
  // Policy row of every live hand at a river node.
  [[nodiscard]] std::span<const std::uint32_t, live_hand_count> rows() const noexcept {
    return rows_;
  }
  [[nodiscard]] std::uint32_t maximum_row() const noexcept { return maximum_row_; }

private:
  card_abstraction::BoardHistory history_{};
  std::array<std::uint16_t, live_hand_count> combo_ids_{};
  std::array<std::array<std::uint8_t, 2>, live_hand_count> cards_{};
  std::array<std::uint16_t, live_hand_count> ranks_{};
  std::array<std::uint16_t, live_hand_count> order_{};
  std::array<std::uint16_t, live_hand_count + 1U> group_starts_{};
  std::uint16_t rank_groups_{0U};
  std::array<std::uint32_t, live_hand_count> rows_{};
  std::uint32_t maximum_row_{no_row};
};

// fold_mass and showdown_masses of kernels.hpp on both lanes at once: lane h
// of every output equals the scalar kernel applied to lane h of the reach,
// bit for bit. The outputs must not alias the reach.
void fold_mass_pair(const RiverBoard &board, ConstPairSpan reach, PairSpan disjoint_mass) noexcept;
void showdown_masses_pair(const RiverBoard &board, ConstPairSpan reach, PairSpan worse,
                          PairSpan tied, PairSpan better) noexcept;

// ValueTraversal of a river subtree for both heroes and both modes at once,
// against a bucket policy (heads-up games only). The scratch of a call (level
// buffers, policy copies, terminal masses, the board pointer) lives in
// members, so an instance is not re-entrant and must not be shared between
// threads: use one per thread, as the evaluator does with one per
// evaluate_flop call.
class JointRiverTraversal {
public:
  JointRiverTraversal(const CompiledGame &game, const BucketPolicy &policy);

  // Lane h of `reach` is the opponent reach of hero h at the river node
  // `node`; lane h of `response` and `average` receives the values of hero h
  // with the best action and with the policy at its own decisions. Each lane
  // equals ValueTraversal::evaluate_from(node, policy, h, lane h of reach)
  // in the corresponding mode on a BoardContext of the same board and
  // tables, bit for bit. The call fails exactly when one of those reference
  // evaluations fails, that is when a policy row it reads lies outside the
  // layout, but the error code may differ from the reference's; evaluate_flop
  // cannot show the difference, as it maps every failure to InvalidInput.
  // Outputs must not alias reach.
  [[nodiscard]] Result<bool, KernelError> evaluate(std::uint32_t node, const RiverBoard &board,
                                                   ConstPairSpan reach, PairSpan response,
                                                   PairSpan average);

private:
  // Buffers of one depth below the root, allocated on first use.
  struct Level {
    std::vector<double> child_reach;    // pair layout
    std::vector<double> child_response; // pair layout
    std::vector<double> child_average;  // pair layout
    std::vector<double> policy;         // [action][hand], maximum_actions x 465
  };

  bool traverse(std::uint32_t node_id, std::size_t level, const double *reach, double *response,
                double *average);
  bool decision(const CompiledNode &node, std::size_t level, unsigned alive, const double *reach,
                double *response, double *average);
  bool gather(const CompiledNode &node, bool all_hands, std::size_t other, const double *reach,
              double *policy);
  void fold(const CompiledNode &node, unsigned alive, const double *reach, double *response,
            double *average);
  void showdown(const CompiledNode &node, unsigned alive, const double *reach, double *response,
                double *average);

  const CompiledGame *game_;
  const BucketPolicy *policy_;
  const RiverBoard *board_{nullptr};
  KernelError error_{KernelError::MissingTable};
  std::vector<Level> levels_;
  std::vector<double> worse_;
  std::vector<double> tied_;
  std::vector<double> better_;
  std::array<std::uint16_t, live_hand_count> gathered_{};
};

} // namespace gtosd::preflop_blueprint
