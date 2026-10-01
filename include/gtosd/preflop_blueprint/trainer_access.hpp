#pragma once

#include "gtosd/preflop_blueprint/abstract_best_response.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Friend entry points of the trainer for the abstract best response and for
// the tests of the 3-seat path (PHASE3_SPEC_2026-09-30, V8 and V13). Nothing
// here is used in production: the hooks run the production functions
// (traverse3 with the top phase and the units, terminal3) on injected reach.
namespace gtosd::preflop_blueprint {

// One hero decision visited by a 3-seat traversal, reported after the child
// values are known and before the regret update. Spans are valid during the
// call only.
struct HeroDecisionTrace {
  std::uint32_t node{0U};
  std::uint8_t hero{0U};
  std::uint8_t actions{0U};
  // v_a[h], action-major: actions x live_hand_count.
  std::span<const double> action_values;
  // v[h] = sum_a sigma(a|h) v_a[h].
  std::span<const double> values;
  // Reach of the hero at the node.
  std::span<const double> hero_reach;
  // Weight of the regret increments of each hand at this node, 0 where the
  // update is skipped (locked class, or the other seats' reach is zero).
  std::span<const double> regret_weight;
};

class HeroDecisionSink {
public:
  virtual ~HeroDecisionSink() = default;
  // The trainer serializes the calls (one at a time, from any worker thread).
  virtual void record(const HeroDecisionTrace &trace) = 0;
};

struct SubtreeValues3 {
  // Hero values at the subtree root, per live hand of the board.
  std::vector<double> values;
  // Weight of the regret increments per live hand: board weight 1, iteration
  // weight 1, P(h) P(o1,o2|h).
  std::vector<double> regret_weight;
  std::uint32_t units_run{0U};
  std::uint32_t units_skipped{0U};
};

class TrainerAccess {
public:
  [[nodiscard]] static Result<AbstractBestResponseReport, TrainerError>
  abstract_best_response(const CompiledGame &game, BucketPolicy policy,
                         const TrainerResources &resources, const std::vector<FlopGroup> &groups,
                         const AbstractBestResponseOptions &options);

  // Replaces the regret table (as double; rounded through the storage type).
  // With lazy discount the trainer must not have iterated yet.
  [[nodiscard]] static Result<bool, TrainerError> set_regrets(Trainer &trainer,
                                                              std::span<const double> regrets);

  // 3-seat path: runs traverse3 from `node` on `board` with the given reach
  // (one vector of live_hand_count per seat, in the BoardContext order of the
  // board, seat order), the production top phase and the units below the node
  // (skipped units excluded), with the current policy of the regrets (one
  // refresh for this board) and regret and strategy-sum writes. The sink, if
  // any, sees every hero decision. The hero must be active at `node` unless the
  // trainer runs with hero_folded_shortcut = false.
  [[nodiscard]] static Result<SubtreeValues3, TrainerError>
  subtree_values3(Trainer &trainer, std::uint32_t node,
                  const std::array<std::vector<double>, 3> &reach, std::uint8_t hero,
                  const card_abstraction::BoardHistory &board, HeroDecisionSink *sink = nullptr);

  // 3-seat path: terminal3 of each terminal node in `nodes` for `hero` on
  // `board` with the given reach (as above); one vector of live_hand_count
  // values per node. Postflop terminals, and preflop terminals in
  // board_kernels mode or in the harness; a preflop terminal in class-cache
  // mode reads the class values of the current policy instead of the reach and
  // is refused (InvalidConfiguration). The hero must be active at each node.
  [[nodiscard]] static Result<std::vector<std::vector<double>>, TrainerError>
  terminal3_values(Trainer &trainer, std::span<const std::uint32_t> nodes,
                   const std::array<std::vector<double>, 3> &reach, std::uint8_t hero,
                   const card_abstraction::BoardHistory &board);

  // Hero values at the root of the last 3-seat pass (diagnostics).
  [[nodiscard]] static const std::vector<double> &last_root_values3(const Trainer &trainer) {
    return trainer.root_values3_;
  }
};

} // namespace gtosd::preflop_blueprint
