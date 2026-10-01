#pragma once

#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"

#include <cstdint>
#include <memory>

// Three-seat terminal kernels (phase 3 spec, sections 3.4-3.7).
//
// A hero hand h = {x, y} sits on the board of a BoardContext; the two other seats hold hands
// o1 and o2 among the same 465 live hands, with reach vectors r1 and r2. Every kernel returns,
// for every hero hand h, masses summed over the ordered pairs (o1, o2) with h, o1 and o2
// mutually disjoint (and disjoint from the board, which every live hand is):
//
//   mass[h] = sum r1(o1) * r2(o2) over the pairs whose showdown outcome is the named one.
//
// The masses are not normalized: the trainer multiplies them by its pair weight
// (1/(406 * 351) per pair) and by the payoffs. The cost is O(n * 36) per kernel call, through
// the pair primitive of spec 3.4: for hand sets S1 and S2,
//
//   M(S1,S2)[h] = A_h * B_h - sum_{c not in h} A_h(c) * B_h(c) + P_h
//
// with A_h the r1-mass of S1 disjoint from h, A_h(c) the part of it holding card c, and P_h the
// r1*r2 mass of the hands in both sets disjoint from h; the sets are maintained as per-card sums
// and 36x36 pair matrices during one ascending sweep over the rank groups of the board.
//
// Seat convention (as in step 1): the "lower" and "higher" vectors of a three-active terminal
// are the hero's two other seats in increasing seat order. Values only weakly separate the two
// (their tie payoffs differ by at most one money unit), so the tests compare the masses one by
// one: a lower/higher swap shows as tie_lower <-> tie_higher.
//
// A folded seat's vector is its reach frozen when it folded: its cards are dead (removed jointly
// with the board, h and the active opponent), never ignored.
//
// Rounding: lose[h] comes out of a subtraction (deal - the other masses). When its exact value is
// 0 the computed value can be -O(1e-16 * deal[h]); it is not clamped. Compare with a tolerance
// scaled by max(1, deal[h]).
//
// Determinism: a kernel call is single-threaded and its summation order is fixed by the board,
// the inputs and the instruction set of the scratch (MultiwayScratch::isa()). The AVX2 and scalar
// paths agree to rounding, not bit for bit; a run must keep one path (the default, the best
// available, is a property of the machine).
namespace gtosd::preflop_blueprint {

enum class MultiwayKernelIsa : std::uint8_t { Scalar, Avx2 };

// True when the CPU and the operating system support AVX2 and FMA.
[[nodiscard]] bool multiway_kernel_avx2_available() noexcept;
// Avx2 when available, else Scalar.
[[nodiscard]] MultiwayKernelIsa best_multiway_kernel_isa() noexcept;
[[nodiscard]] const char *multiway_kernel_isa_name(MultiwayKernelIsa isa) noexcept;
// Version tag of the kernel arithmetic, for trainer identities.
inline constexpr const char *multiway_kernel_version = "multiway-kernel-v1";

// Per-thread working memory of the kernels: six per-card sums and 36x36 pair matrices (the
// Below and Group sets of both other seats, the static set of a folded seat), two product sums
// and five per-hand buffers for the value kernels, about 90 KB. Not shared between threads; a
// kernel call leaves it ready for the next call.
class MultiwayScratch {
public:
  // An Avx2 request on a machine without AVX2 falls back to Scalar (see isa()).
  explicit MultiwayScratch(MultiwayKernelIsa isa = best_multiway_kernel_isa());
  ~MultiwayScratch();
  MultiwayScratch(MultiwayScratch &&) noexcept;
  MultiwayScratch &operator=(MultiwayScratch &&) noexcept;
  MultiwayScratch(const MultiwayScratch &) = delete;
  MultiwayScratch &operator=(const MultiwayScratch &) = delete;

  [[nodiscard]] MultiwayKernelIsa isa() const noexcept { return isa_; }
  [[nodiscard]] static std::size_t bytes() noexcept;

  struct Storage;
  [[nodiscard]] Storage &storage() noexcept { return *storage_; }

private:
  MultiwayKernelIsa isa_{MultiwayKernelIsa::Scalar};
  std::unique_ptr<Storage> storage_;
};

// Outputs of the three-active showdown kernel (spec 3.5). For the hero's other seats, lower (r1)
// and higher (r2):
//   win        : h beats both;
//   tie_lower  : h ties the lower seat and beats the higher one;
//   tie_higher : h beats the lower seat and ties the higher one;
//   tie_both   : h ties both;
//   lose       : at least one seat beats h (= deal - the four above).
// The hero's value is win * p({h}) + tie_lower * p({h, lower}) + tie_higher * p({h, higher})
// + tie_both * p({h, lower, higher}) + lose * p_lose, the payoffs read per winner set.
struct ThreeActiveMassSpans {
  HandSpan win;
  HandSpan tie_lower;
  HandSpan tie_higher;
  HandSpan tie_both;
  HandSpan lose;
};

// Outputs of the two-active showdown kernel (spec 3.6): the opponent o is active, the folded
// seat f is not; f's cards are dead.
//   win  : h beats o;   tie : h ties o;   lose : o beats h (= deal - win - tie).
// The hero's value is win * p({h}) + tie * p({h, o}) + lose * p({o}).
struct TwoActiveMassSpans {
  HandSpan win;
  HandSpan tie;
  HandSpan lose;
};

// Hero payoffs in antes by winner set, for the value kernels.
struct ThreeActivePayoffs {
  double win{0.0};
  double tie_lower{0.0};
  double tie_higher{0.0};
  double tie_both{0.0};
  double lose{0.0};
};
struct TwoActivePayoffs {
  double win{0.0};
  double tie{0.0};
  double lose{0.0};
};

// D3 (spec 3.2, 3.7): deal[h] = sum r1(o1) r2(o2) over every disjoint pair, whatever the ranks.
// Symmetric in its two vectors. Fold terminals (payoff * D3), the hero-folded shortcut and the
// folded seat's side of the V8 identity use it.
void three_seat_deal_mass(const BoardContext &context, ConstHandSpan first, ConstHandSpan second,
                          HandSpan deal, MultiwayScratch &scratch) noexcept;

// Three-active showdown or all-in runout on the full five-card board (spec 3.5).
void three_active_masses(const BoardContext &context, ConstHandSpan lower, ConstHandSpan higher,
                         const ThreeActiveMassSpans &out, MultiwayScratch &scratch) noexcept;

// Two-active showdown or runout with the third seat folded, its cards dead (spec 3.6).
void two_active_masses(const BoardContext &context, ConstHandSpan opponent, ConstHandSpan folded,
                       const TwoActiveMassSpans &out, MultiwayScratch &scratch) noexcept;

// values[h] = the masses above times the payoffs; the masses go through the scratch's buffers.
void three_active_values(const BoardContext &context, ConstHandSpan lower, ConstHandSpan higher,
                         const ThreeActivePayoffs &payoffs, HandSpan values,
                         MultiwayScratch &scratch) noexcept;
void two_active_values(const BoardContext &context, ConstHandSpan opponent, ConstHandSpan folded,
                       const TwoActivePayoffs &payoffs, HandSpan values,
                       MultiwayScratch &scratch) noexcept;

// O(n^3) references: explicit enumeration of the triples (h, o1, o2) with the board's ranks. They
// skip zero-reach first hands and exist for the tests (seconds per call on dense reach).
void three_seat_deal_mass_reference(const BoardContext &context, ConstHandSpan first,
                                    ConstHandSpan second, HandSpan deal) noexcept;
void three_active_masses_reference(const BoardContext &context, ConstHandSpan lower,
                                   ConstHandSpan higher, const ThreeActiveMassSpans &out) noexcept;
void two_active_masses_reference(const BoardContext &context, ConstHandSpan opponent,
                                 ConstHandSpan folded, const TwoActiveMassSpans &out) noexcept;

// Floating-point operations per hero hand of each kernel, counted from the loops of this
// implementation (the K4 benchmark divides them by the measured time). Singleton rank groups use
// the shorter loop of the three-active kernel.
struct MultiwayKernelFlops {
  double deal{0.0};
  double three_active_group{0.0};
  double three_active_singleton{0.0};
  double two_active{0.0};
};
[[nodiscard]] MultiwayKernelFlops multiway_kernel_flops_per_hand() noexcept;

} // namespace gtosd::preflop_blueprint
