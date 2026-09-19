#pragma once

#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"

#include <cstdint>
#include <span>
#include <vector>

// Value propagation along the compiled tree for one board.
//
// The traversal computes, for the hero, the counterfactual value of every
// live hand at the root: v[h] = sum over terminals z and opponent hands o
// disjoint from h of reach_opp(o, z) * u_hero(h, o, z), in antes, where the
// opponent reach follows the policy along the path and the hero follows the
// policy (or the best action, in best-response mode) at its own nodes. The
// chance factors 1/465 and 1/406 are constant on a board and left to the
// caller. No allocation happens after construction.
namespace gtosd::preflop_blueprint {

// Action probabilities of a player for a hand at a decision node.
class Policy {
public:
  virtual ~Policy() = default;
  // Pointer to action_count(node) probabilities; valid until the next call.
  [[nodiscard]] virtual const double *probabilities(std::uint32_t node, std::uint16_t hand,
                                                    const BoardContext &context) const noexcept = 0;
};

// Dense table on the (node, row, action) layout of the state: the row is the
// preflop hand class or the street bucket of the hand on the board.
class BucketPolicy final : public Policy {
public:
  BucketPolicy(const CompiledGame &game, const StateLayout &layout);
  BucketPolicy(const CompiledGame &game, const StateLayout &layout, std::vector<double> table);
  void set_uniform();
  [[nodiscard]] std::span<double> row(std::uint32_t node, std::uint32_t row_index) noexcept;
  [[nodiscard]] std::span<const double> row(std::uint32_t node,
                                            std::uint32_t row_index) const noexcept;
  [[nodiscard]] const StateLayout &layout() const noexcept { return layout_; }
  [[nodiscard]] std::vector<double> &table() noexcept { return table_; }
  [[nodiscard]] const std::vector<double> &table() const noexcept { return table_; }
  [[nodiscard]] const double *probabilities(std::uint32_t node, std::uint16_t hand,
                                            const BoardContext &context) const noexcept override;

private:
  const CompiledGame *game_;
  StateLayout layout_;
  std::vector<double> table_;
};

// Dense table on (node, live hand, action) for a fixed board: used by tests
// and oracles that prescribe a strategy per combo.
class HandPolicy final : public Policy {
public:
  explicit HandPolicy(const CompiledGame &game);
  void set_uniform();
  [[nodiscard]] std::span<double> row(std::uint32_t node, std::uint16_t hand) noexcept;
  [[nodiscard]] const double *probabilities(std::uint32_t node, std::uint16_t hand,
                                            const BoardContext &context) const noexcept override;

private:
  const CompiledGame *game_;
  std::vector<std::uint64_t> offsets_;
  std::vector<double> table_;
};

struct TraversalOptions {
  // Hero takes the best action at its nodes instead of following the policy.
  bool best_response{false};
};

struct TraversalCounters {
  std::uint64_t nodes_visited{0U};
  std::uint64_t subtrees_pruned{0U};
  std::uint64_t terminals_evaluated{0U};
};

class ValueTraversal {
public:
  // all_in may be null when the game has no preflop all-in terminal reachable
  // with positive reach; evaluating such a terminal then fails the traversal.
  ValueTraversal(const CompiledGame &game, const BoardContext &context,
                 const ShowdownKernel &kernel, const AllInEquityCache *all_in);
  // Points the traversal at another board of the same game, keeping the
  // workspace.
  void rebind(const BoardContext &context, const AllInEquityCache *all_in) noexcept {
    context_ = &context;
    all_in_ = all_in;
  }

  // Root counterfactual values of hero in antes for the given opponent reach.
  [[nodiscard]] Result<bool, KernelError> evaluate(const Policy &policy, std::uint8_t hero,
                                                   ConstHandSpan opponent_reach, HandSpan values,
                                                   const TraversalOptions &options = {});
  // Same evaluation started at an arbitrary node.
  [[nodiscard]] Result<bool, KernelError> evaluate_from(std::uint32_t node, const Policy &policy,
                                                        std::uint8_t hero,
                                                        ConstHandSpan opponent_reach,
                                                        HandSpan values,
                                                        const TraversalOptions &options = {});
  [[nodiscard]] const TraversalCounters &counters() const noexcept { return counters_; }

private:
  struct Level {
    std::vector<double> child_reach; // maximum_actions x 465
    std::vector<double> child_values; // maximum_actions x 465
    std::vector<double> scratch; // 3 x 465
  };

  bool traverse(std::uint32_t node, std::uint32_t depth, const double *opponent_reach,
                double *values);
  void terminal(const CompiledNode &node, const double *opponent_reach, double *values,
                double *scratch);

  const CompiledGame *game_;
  const BoardContext *context_;
  const ShowdownKernel *kernel_;
  const AllInEquityCache *all_in_;
  const Policy *policy_{nullptr};
  std::uint8_t hero_{0U};
  bool best_response_{false};
  bool failed_{false};
  std::vector<Level> levels_;
  TraversalCounters counters_{};
};

} // namespace gtosd::preflop_blueprint
