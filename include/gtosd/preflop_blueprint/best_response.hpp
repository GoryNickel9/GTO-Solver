#pragma once

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <array>
#include <cstdint>
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
// Boards are given in groups by flop. Sampled evaluation draws physical
// flops and enumerates all their runouts (33 x 32 boards per flop); an
// explicit board list is grouped by flop identity.
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
};

struct BestResponseOptions {
  unsigned threads{1U};
  // Optional uniform hand subsets per player (combo ids); empty = all hands.
  std::array<std::vector<std::uint16_t>, 2> hand_subsets{};
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
  // upwards by the selection noise (about 1/sqrt(flops)); the two coincide
  // with the exact best response when all flops are enumerated.
  std::array<double, 2> best_response_lower{};
  std::array<double, 2> gain_lower{};
  // Standard errors of the means over equally weighted flop groups of the
  // per-flop values of the chosen best response and of the average strategy;
  // zero for one group or for unequal group weights.
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

[[nodiscard]] Result<BestResponseReport, KernelError>
evaluate_best_response(const CompiledGame &game, const BucketPolicy &average,
                       const BestResponseResources &resources,
                       const std::vector<FlopGroup> &groups,
                       const BestResponseOptions &options = {});

} // namespace gtosd::preflop_blueprint
