#pragma once

#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/core/result.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Exact values of the preflop terminals of a 3-player game for the step-2
// trainer: the per-pass class cache of the phase-3 spec (section 3.9), built
// on the three-player class table (preflop_three_way_v1.bin), folded cards
// dead.
//
// Why it is exact. Preflop rows are the 81 hand classes and the initial reach
// is one, so at every preflop node every seat's reach is constant over the
// combos of a class. A seat's value at a preflop terminal then depends only on
// its class and on the class reach of the other seats, and the class table
// gives the expectation over every runout (step 1, benchmarks/
// checkdown_classes.hpp, whose tensor builder this is a port of).
//
// Terms. Only nodes whose street is Preflop get terms:
//  - every preflop TerminalFold: every seat's fold payoff times the class D3
//    (Deal term);
//  - every preflop TerminalShowdown (an all-in with five cards to come, or a
//    checkdown leaf of a tree compiled with checkdown_at_flop): with 3 active
//    seats one tensor per seat, the runout-weighted masses of the winner sets
//    {seat}, {seat, lower}, {seat, higher}, {all} and "seat loses" times the
//    seat's payoff for each (the loser payoff is checked equal over the three
//    sets without the seat); with 2 active seats the folder gets a Deal term
//    with its payoff (checked equal over the winner sets) and each active seat
//    a tensor of its win, tie and loss masses against the active opponent with
//    the folder's cards dead, transposed to [H][lower][higher] when the folder
//    is the lower other seat;
//  - every non-terminal preflop node right after a seat's fold (the seat is no
//    longer active there): that seat's payoff, checked equal at every terminal
//    of the subtree (preflop and postflop), times the class D3. This is the
//    hero-folded shortcut of section 3.2 for preflop folds.
// Postflop nodes never get a term (a postflop fold or showdown is evaluated on
// the board by the multiway kernels). The tensors of the 3- and 2-active
// showdowns are built with the same arithmetic as three_way_terminals (byte
// identical per preflop node id on a checkdown-compiled tree).
//
// Layout and scale. A tensor is [H][A][B] with A the class of the lower other
// seat and B of the higher one (ThreeWayTable::entry_index). The value of a
// seat at a node, per class H, is
//   Tensor: board_scale * sum_{A,B} reach_lower[A] reach_higher[B] tensor[H][A][B]
//   Deal:   (payoff * board_scale) * D3[H]
// with D3[H] the deal mass of the other two seats' combos disjoint from the
// representative of H and from each other (inclusion-exclusion over shared
// cards, three_way_deal_values of step 1), payoffs in antes. The class values
// sum over all 561 x 496 = 278,256 ordered disjoint pairs, while the trainer
// weights a deal by P(o1, o2 | h) = 1 / (406 x 351) = 1 / 142,506 and sums only
// over the pairs live on the sampled board; for a fixed hero combo the
// probability that both other combos are live on a uniform board where the
// hero is live is C(30,5) / C(34,5) = 142,506 / 278,256, so board_scale =
// 142,506 / 278,256 (about 0.512140) keeps the cache unbiased under the board-
// conditioned weight (tower property, as heads-up relies on 406 / 561).
//
// Per hero pass, after the policy refresh (which has applied any preflop
// lock): compute_reach(policy, offsets) from the preflop rows of the current
// policy, then contract(hero), then values(node, hero) for every node where
// the trainer needs the hero's preflop value. All 81 preflop rows of every
// preflop decision must be present in the policy (the trainer's compact
// policy always materializes them: collect_active_rows). Each term is
// contracted in one piece in a fixed order, so the values do not depend on
// the thread count.
namespace gtosd::preflop_blueprint {

enum class PreflopClassCacheError : std::uint8_t {
  NotThreePlayers,
  IncompleteTable,
  UnexpectedTerminal,
  PayoffMismatch,
  CountMismatch
};

enum class PreflopTermKind : std::uint8_t { None, Deal, Tensor };

// Value of one seat at one node.
struct PreflopClassTerm {
  PreflopTermKind kind{PreflopTermKind::None};
  // Index into PreflopClassCache::tensors() (Tensor terms).
  std::uint32_t tensor{0U};
  // The seat's payoff in antes (Deal terms): money units / units per ante, as
  // step 1 computes it.
  double payoff{0.0};
};

struct PreflopClassCacheCounts {
  // Preflop TerminalShowdown nodes reached by an all-in (status AllInRunout).
  std::uint32_t all_in_terminals{0U};
  // Preflop TerminalShowdown nodes that are flop entries settled as a
  // checkdown (CompileOptions::checkdown_at_flop).
  std::uint32_t checkdown_leaves{0U};
  std::uint32_t fold_terminals{0U};
  // Showdowns (all-ins and leaves) by number of active seats.
  std::uint32_t three_active_showdowns{0U};
  std::uint32_t two_active_showdowns{0U};
  // Non-terminal preflop nodes right after a fold (one Deal term each).
  std::uint32_t folded_children{0U};
  std::uint32_t tensors{0U};
  // Terms per seat (Deal and Tensor).
  std::array<std::uint32_t, 3> terms_by_seat{};
};

class PreflopClassCache {
public:
  static constexpr std::size_t class_count = 81U;
  static constexpr std::size_t seat_count = 3U;
  // C(30,5) / C(34,5): see the header comment.
  static constexpr double board_scale = 142'506.0 / 278'256.0;
  static constexpr std::uint32_t no_slot = 0xFFFF'FFFFU;

  // The game must have 3 players and the table must be complete. Fails with
  // UnexpectedTerminal on a preflop showdown with fewer than 5 cards to come or
  // fewer than 2 active seats, PayoffMismatch when a losing or folded seat has
  // more than one payoff, CountMismatch when the terms do not cover exactly
  // the preflop terminals the compiler counted.
  [[nodiscard]] static Result<PreflopClassCache, PreflopClassCacheError>
  build(const CompiledGame &game, const card_abstraction::ThreeWayTable &table);

  // Class reach of every seat at every preflop node from the preflop rows of a
  // policy: row (node, class) starts at policy[node_offsets[node] + class *
  // action_count] in edge order (the trainer's compact policy and the dense
  // layout both have this form for preflop rows). The root has reach one.
  void compute_reach(std::span<const double> policy, std::span<const std::uint64_t> node_offsets);

  // Fills the values of `hero` at every node where it has a term, from the
  // reach of the last compute_reach. Terms are spread over `threads` workers,
  // each term computed in one piece: the values do not depend on `threads`.
  void contract(std::uint8_t hero, unsigned threads = 1U);

  // The 81 class values of `seat` at `node` from the last contract(seat), in
  // antes, already scaled by board_scale; nullptr where the seat has no term.
  [[nodiscard]] const double *values(std::uint32_t node, std::uint8_t seat) const noexcept;
  // The 81 class reach values of `seat` at a preflop node from the last
  // compute_reach; nullptr for a postflop node.
  [[nodiscard]] const double *reach(std::uint32_t node, std::uint8_t seat) const noexcept;
  [[nodiscard]] const PreflopClassTerm &term(std::uint32_t node, std::uint8_t seat) const noexcept;
  [[nodiscard]] const std::vector<std::vector<double>> &tensors() const noexcept {
    return tensors_;
  }
  [[nodiscard]] const PreflopClassCacheCounts &counts() const noexcept { return counts_; }
  // "fnv1a64:<hex>" over the format tag, the table and game fingerprints, the
  // folded-cards convention, the scale and the counts.
  [[nodiscard]] const std::string &fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] std::uint64_t tensor_bytes() const noexcept;
  // Tensors, reach, values and the per-node tables.
  [[nodiscard]] std::uint64_t memory_bytes() const noexcept;

  // Class D3 of `hero` at a preflop node from the current reach, unscaled:
  // the deal mass of the two other seats' combos disjoint from the
  // representative of each class and from each other (out: 81 values).
  void deal_values(std::uint32_t node, std::uint8_t hero, double *out) const;

  // Static forms (tests, evaluators).
  // D3[H] for the reach of the lower and the higher other seat, by
  // inclusion-exclusion over shared cards (three_way_deal_values of step 1).
  static void class_deal_values(const double *lower, const double *higher, double *out);
  // out[H] = scale * sum_{A,B} tensor[H][A][B] lower[A] higher[B] in four
  // interleaved partial sums (contract_interleaved of step 1); `outer` is a
  // scratch of 81 x 81 doubles.
  static void contract_tensor(std::span<const double> tensor, double scale, const double *lower,
                              const double *higher, double *outer, double *out);

private:
  struct Entry {
    std::uint32_t node{0U};
    std::uint32_t slot{0U};
  };

  const CompiledGame *game_{nullptr};
  std::vector<std::vector<double>> tensors_;
  // Per node id and seat (node * 3 + seat).
  std::vector<PreflopClassTerm> terms_;
  std::vector<std::uint32_t> value_slot_;
  // Per node id: index of the preflop node in reach_, or no_slot.
  std::vector<std::uint32_t> preflop_index_;
  // Preflop nodes in increasing id (preorder: a parent comes first) and the
  // edge of each from its parent (0 for the root).
  std::vector<std::uint32_t> preflop_nodes_;
  std::vector<std::uint8_t> parent_edge_;
  // reach_[(index * 3 + seat) * 81 + class].
  std::vector<double> reach_;
  // values_[slot * 81 + class].
  std::vector<double> values_;
  // Terms of each seat in increasing node id.
  std::array<std::vector<Entry>, 3> entries_;
  PreflopClassCacheCounts counts_{};
  std::string fingerprint_;
};

[[nodiscard]] const char *preflop_class_cache_error_name(PreflopClassCacheError error) noexcept;

} // namespace gtosd::preflop_blueprint
