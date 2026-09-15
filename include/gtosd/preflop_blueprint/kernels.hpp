#pragma once

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"

#include <cstdint>
#include <span>
#include <vector>

// Vector kernels of the terminal values (roadmap section 5).
//
// Every kernel takes the opponent reach vector over the 465 live hands and
// returns, for every hero hand h, masses summed over the opponent hands o
// disjoint from h:
//   fold:      D[h] = sum_o r[o]
//   showdown:  W[h] (o worse than h), T[h] (same rank), L[h] (o better)
//   all-in:    W/T/L weighted by the exact runout probabilities of the pair.
// The sweep implementations cost O(n log n) per board through per-card
// blocker corrections; the reference implementations enumerate the pairs
// and exist for the tests. Nothing here depends on the number of players:
// the ShowdownKernel interface receives N reach vectors and the mask of the
// live players; the heads-up implementation is the only one provided.
namespace gtosd::preflop_blueprint {

using HandSpan = std::span<double, live_hand_count>;
using ConstHandSpan = std::span<const double, live_hand_count>;

// D[h] = S - C[h1] - C[h2] + r[h] with S the total mass and C[c] the mass of
// the hands containing card c.
void fold_mass(const BoardContext &context, ConstHandSpan reach, HandSpan disjoint_mass) noexcept;
void fold_mass_reference(const BoardContext &context, ConstHandSpan reach,
                         HandSpan disjoint_mass) noexcept;

// Ascending and descending sweeps over the rank order with running per-card
// sums; hands of equal rank are processed as one group whose masses join the
// running sums only after every member has read them.
void showdown_masses(const BoardContext &context, ConstHandSpan reach, HandSpan worse,
                     HandSpan tied, HandSpan better) noexcept;
void showdown_masses_reference(const BoardContext &context, ConstHandSpan reach, HandSpan worse,
                               HandSpan tied, HandSpan better) noexcept;

// Exact preflop all-in outcomes of every live pair on a board, read once from
// the P2 table: win and tie probabilities over the 201,376 runouts of the pair.
class AllInEquityCache {
public:
  [[nodiscard]] static Result<AllInEquityCache, KernelError>
  build(const BoardContext &context, const card_abstraction::AllInTable &table);

  // W[h] = sum_o r[o] p_win(h, o), T[h] = sum_o r[o] p_tie(h, o), L = D - W - T.
  void masses(const BoardContext &context, ConstHandSpan reach, HandSpan win, HandSpan tie,
              HandSpan lose) const noexcept;
  void masses_reference(const BoardContext &context, const card_abstraction::AllInTable &table,
                        ConstHandSpan reach, HandSpan win, HandSpan tie,
                        HandSpan lose) const noexcept;
  [[nodiscard]] std::uint64_t payload_bytes() const noexcept {
    return (win_.size() + tie_.size()) * sizeof(double);
  }

private:
  // Row-major 465 x 465; zero on the diagonal and for overlapping pairs.
  std::vector<double> win_;
  std::vector<double> tie_;
};

// Showdown kernel interface (decision D13): N reach vectors, live mask, hero.
class ShowdownKernel {
public:
  virtual ~ShowdownKernel() = default;
  [[nodiscard]] virtual std::uint8_t maximum_players() const noexcept = 0;
  // reach[p] is the reach vector of player p; only players in live_mask are
  // read. Outputs are the masses of the opponents worse, tied and better.
  virtual void evaluate(const BoardContext &context, std::uint8_t live_mask, std::uint8_t hero,
                        std::span<const ConstHandSpan> reach, HandSpan worse, HandSpan tied,
                        HandSpan better) const noexcept = 0;
};

class HeadsUpShowdownKernel final : public ShowdownKernel {
public:
  [[nodiscard]] std::uint8_t maximum_players() const noexcept override { return 2U; }
  void evaluate(const BoardContext &context, std::uint8_t live_mask, std::uint8_t hero,
                std::span<const ConstHandSpan> reach, HandSpan worse, HandSpan tied,
                HandSpan better) const noexcept override;
};

} // namespace gtosd::preflop_blueprint
