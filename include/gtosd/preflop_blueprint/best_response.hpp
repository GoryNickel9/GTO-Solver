#pragma once

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Best response of the physical game against the lifted (bucket) average
// strategy, with the information structure of the physical game: a decision
// of the best responder at a street may depend on its hand and on the cards
// dealt so far, never on later cards. Values are therefore aggregated over
// the future cards before the maximum is taken:
//
//   river:   maximum per hand on the full board (P5 traversal, max mode);
//   turn:    action values summed over the rivers, then maximum;
//   flop:    summed over the turns, then maximum;
//   preflop: summed over the flops, then maximum.
//
// The opponent follows the average strategy, whose street buckets depend
// only on the cards dealt so far, so the values of a street are linear in
// the values of its leaves and the aggregation needs no per-runout storage
// above the river. Taking the maximum per full board above the river would
// give a clairvoyant responder and overstate the exploitability (P6 diary).
//
// The work splits in two stages: the values of every hand at the postflop
// entries for one flop group (all the listed runouts of that flop), and the
// preflop aggregation over a set of flop groups. A flop group may stand for
// its whole suit orbit (P7 certifier): the strategy is suit-symmetric, so
// the values of hand h on the image sigma(flop) are the values of
// sigma^-1(h) on the flop.
namespace gtosd::preflop_blueprint {

struct WeightedBoard {
  card_abstraction::BoardHistory history{};
  double weight{1.0};
};

struct FlopGroup {
  std::array<CardId, 3> flop{};
  std::vector<WeightedBoard> boards;
  double weight{1.0};
};

struct BestResponseResources {
  const card_abstraction::RankTable *ranks{nullptr};
  const card_abstraction::AllInTable *all_in{nullptr};
  const card_abstraction::BoardCatalog *catalog{nullptr};
  const card_abstraction::BucketTable *flop{nullptr};
  const card_abstraction::BucketTable *turn{nullptr};
  const card_abstraction::BucketTable *river{nullptr};
  const ClassBucketRows *class_rows{nullptr};
};

struct BestResponseOptions {
  unsigned threads{1U};
  // Optional uniform hand subsets per player (combo ids); empty = all hands.
  std::array<std::vector<std::uint16_t>, 2> hand_subsets{};
};

inline constexpr std::size_t response_mode = 0U;
inline constexpr std::size_t average_mode = 1U;

// Values of every combo at the postflop entries for one flop group.
struct FlopValues {
  std::array<CardId, 3> flop{};
  double weight{1.0};
  std::uint32_t boards{0U};
  // Suit permutations whose images of the flop are the physical flops this
  // group stands for; the identity alone for a physical flop.
  std::vector<card_abstraction::SuitPermutation> images{card_abstraction::identity_permutation};
  // 630 entries: 1 when the combo is disjoint from the flop.
  std::vector<std::uint8_t> compatible;
  // [hero][mode][entry] -> 630 values (0 for combos not compatible).
  std::array<std::array<std::vector<std::vector<double>>, 2>, 2> entry_values{};
};

struct BestResponseReport {
  std::uint32_t flops{0U};
  std::uint32_t boards{0U};
  // Values in antes per hand under the average strategy (ev) and under the
  // best response of that player against the average strategy of the other.
  std::array<double, 2> ev{};
  std::array<double, 2> best_response{};
  std::array<double, 2> gain{};
  // Lower bound without selection: the hero follows the average strategy at
  // the preflop and best-responds from the flop on (exact within every
  // flop). Unbiased for that policy and never below ev, while best_response
  // also maximises the preflop choice on the sampled flops and is biased
  // upwards by the selection noise (about 1/sqrt(flops)). Enumerating all
  // flops removes sampling error from both quantities; the lower bound still
  // freezes the hero's preflop policy and need not equal the unrestricted BR.
  std::array<double, 2> best_response_lower{};
  std::array<double, 2> gain_lower{};
  // Mirror image of best_response_lower: the hero best-responds at the preflop
  // and then plays the average strategy from the flop on. Together with ev and
  // best_response it separates a preflop-only error from one that needs both
  // levels to deviate at once. Always between ev and best_response, because
  // maximising one level cannot beat maximising both.
  std::array<double, 2> best_response_preflop{};
  std::array<double, 2> gain_preflop{};
  // Preflop action mix chosen by the best response, one entry per decision
  // node of the hero still on the preflop street. Frequencies are over the 81
  // hand classes, as in the chart export, so the two can be compared directly.
  // `split_classes` counts classes whose combos disagreed on the action, which
  // suit symmetry forbids and which therefore must come out zero.
  struct PreflopChoiceMix {
    std::uint32_t node{0U};
    std::uint8_t hero{0U};
    std::uint8_t action_count{0U};
    std::uint32_t split_classes{0U};
    std::array<double, maximum_actions> frequency{};
  };
  std::vector<PreflopChoiceMix> best_response_preflop_mix;
  // Loss inside one postflop entry, measured at a fixed reach: the mean over
  // the hero's live combos, uniformly weighted, of the counterfactual gain of
  // best-responding from that entry instead of playing the blueprint. Unlike
  // gain_lower it does not depend on how often the blueprint goes there.
  // `opponent_reach` is a RAW SUM of opponent combo weights, NOT a probability.
  // `entry_probability` averages the blocker-conditioned opponent reach over
  // the allowed hero combos, with the hero's preflop actions forced along the
  // entry path. It is independent of the hero's blueprint reach.
  // `conditional_gain` = mean_gain / entry_probability, in antes per entry,
  // is supplied only for exact evaluation with both full uniform hand ranges.
  // Partial-board evaluation and restricted ranges have different board
  // conditioning; this ratio must not be presented as a physical conditional EV.
  // None of these local diagnostics add up to root exploitability.
  struct PostflopEntryLoss {
    std::uint32_t node{0U};
    std::uint8_t hero{0U};
    double mean_gain{0.0};
    double opponent_reach{0.0};
    double entry_probability{0.0};
    std::optional<double> conditional_gain;
  };
  std::vector<PostflopEntryLoss> postflop_entry_loss;
  // Standard errors of the means over equally weighted flop groups of the
  // per-flop values of the chosen best response and of the average strategy;
  // zero for one group, for unequal group weights or for an exact pass.
  std::array<double, 2> best_response_standard_error{};
  std::array<double, 2> ev_standard_error{};
  double max_gain{0.0};
  double max_gain_lower{0.0};
  double max_gain_half_width{0.0};
  double nashconv{0.0};
  double seconds{0.0};
};

// One group with all 33 x 32 runouts of the flop, each board with weight one.
[[nodiscard]] FlopGroup full_runouts(const std::array<CardId, 3> &flop, double weight = 1.0);
// Groups an explicit board list by flop identity; group weights are the sums
// of the board weights.
[[nodiscard]] std::vector<FlopGroup> group_by_flop(const std::vector<WeightedBoard> &boards);
// One suit permutation per distinct physical image of the flop (its orbit);
// the first one is the identity.
[[nodiscard]] std::vector<card_abstraction::SuitPermutation>
flop_images(const std::array<CardId, 3> &flop);

// Values of the actions of one player at its preflop decision nodes under
// the average strategy (chart export, P8): counterfactual values per combo
// averaged over the flop groups compatible with the combo, the opponent
// reach at the node given the combo, and per hand class the conditional EV
// of every action (combo values weighted by the opponent reach) with its
// standard error over the groups.
struct PreflopActionValues {
  std::uint8_t hero{0U};
  std::uint32_t groups{0U};
  // Preflop decision nodes of the hero, in preorder.
  std::vector<std::uint32_t> nodes;
  // [node][action][combo]: mean counterfactual value of the action.
  std::vector<std::vector<std::vector<double>>> combo_values;
  // [node][combo]: probability that the opponent reaches the node given the
  // combo, in [0, 1].
  std::vector<std::vector<double>> opponent_reach;
  // [node][action][class]: conditional EV (antes) of the action given the
  // class and the history (combo values divided by the opponent reach,
  // combos weighted by that reach) and its standard error over the groups.
  std::vector<std::vector<std::vector<double>>> class_ev;
  std::vector<std::vector<std::vector<double>>> class_se;
  // [node][class]: total opponent-reach weight of the class.
  std::vector<std::vector<double>> class_weight;
};

// Probe of one postflop node during the evaluation of a flop group (query
// worker, P8): the opponent reach at the node and, for a decision node of
// the hero on the flop or turn street, the counterfactual value of every
// action per hero combo under the average strategy, aggregated over the
// runouts of the group. Values follow the preflop convention (opponent reach
// and P(o|h) inside): the EV of an action given the combo is
// action_values[a][h] / opponent_mass[h]. Chance nodes can be probed for
// their reach only (river nodes are evaluated by the worker on the board).
struct NodeProbe {
  std::uint32_t node{no_node};
  std::uint8_t hero{0U};
  bool found{false};
  // 630 entries: P(opponent reaches the node with a hand disjoint from h | h).
  std::vector<double> opponent_mass;
  // 630 entries: opponent reach at the node per opponent combo (0 when dead).
  std::vector<double> opponent_reach;
  // [action][630], decision nodes of the hero only.
  std::vector<std::vector<double>> action_values;
};

// Fixed part of an evaluation: game, average strategy, resources, hand
// subsets and the opponent reach at the preflop leaves. Copies share the
// state and may be used from several threads for evaluate_flop.
class BestResponseEvaluator {
public:
  [[nodiscard]] static Result<BestResponseEvaluator, KernelError>
  create(const CompiledGame &game, const BucketPolicy &average,
         const BestResponseResources &resources,
         const std::array<std::vector<std::uint16_t>, 2> &hand_subsets = {});

  // Stage one, for one flop group (thread-safe).
  [[nodiscard]] Result<FlopValues, KernelError> evaluate_flop(const FlopGroup &group) const;
  // Stage one with a probe of one flop or turn node (see NodeProbe).
  [[nodiscard]] Result<NodeProbe, KernelError> probe_node(const FlopGroup &group, std::uint32_t node,
                                                         std::uint8_t hero) const;
  // Stage two over a set of flop values. With exact = true every combo must
  // be compatible with the same total weight of images (the whole catalog):
  // the standard errors are zero and the report is the exact best response.
  [[nodiscard]] Result<BestResponseReport, KernelError>
  aggregate(const std::vector<const FlopValues *> &flops, bool exact) const;
  [[nodiscard]] std::size_t entry_count() const noexcept;
  // Action values of the hero at its preflop nodes over a set of flop values
  // (equal group weights).
  [[nodiscard]] Result<PreflopActionValues, KernelError>
  preflop_action_values(const std::vector<const FlopValues *> &flops, std::uint8_t hero) const;

  struct Impl;

private:
  std::shared_ptr<const Impl> impl_;
};

// Stage one in parallel over the groups.
[[nodiscard]] Result<std::vector<FlopValues>, KernelError>
evaluate_flops(const BestResponseEvaluator &evaluator, const std::vector<FlopGroup> &groups,
               unsigned threads);

// Sampled or explicit evaluation: stage one in parallel over the groups,
// then stage two with standard errors.
[[nodiscard]] Result<BestResponseReport, KernelError>
evaluate_best_response(const CompiledGame &game, const BucketPolicy &average,
                       const BestResponseResources &resources,
                       const std::vector<FlopGroup> &groups,
                       const BestResponseOptions &options = {});

} // namespace gtosd::preflop_blueprint
