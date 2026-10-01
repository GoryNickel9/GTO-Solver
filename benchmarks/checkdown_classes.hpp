#pragma once

// Class-level solver of the checkdown game (step 1 of MonkerSolver's tree
// building: the preflop tree with an empty postflop) for 2 or 3 seats, shared
// by gtosd_preflop_blueprint_checkdown_classes and its tests.
//
// The checkdown game is invariant under the 24 suit permutations: while every
// seat's reach is constant over the combos of a hand class, every combo of a
// class has the same values, so vector DCFR over the 81 preflop classes is
// the combo-level DCFR of preflop_blueprint_checkdown.cpp up to floating-point
// rounding, not an abstraction. The reach of a class is the reach of each of
// its combos, and a class's values are those of its representative combo
// (the lowest combo id, as in the three-player class table).
//
// Terminals. The values of a seat at a terminal are one tensor over the
// seat's class (major) and the classes of the other seats in seat order,
// contracted with the outer product of the other seats' reach and scaled:
//   values[H] = scale * sum_j tensor[H * 81^(N-1) + j] * outer[j].
// Tensor 0 counts the deals of the other seats' combos disjoint from the
// representative and from each other: D[H][A] heads-up, N[H][A][B] 3-way. A
// seat whose payoff does not depend on the cards (every seat at a fold, a
// seat that folded earlier at a 2-way showdown) uses it scaled by that
// payoff; a seat at a showdown uses a tensor of its own (scale 1): the
// runout-weighted masses of every outcome times the seat's payoff in it,
// divided by the runouts. The heads-up tensors are aggregated from
// preflop_all_in_v1.bin at load; the 3-way ones come from the three-player
// class table (preflop_three_way_v1.bin) in the same layout. With 3 seats the
// deal count tensor is never read in the traversal: its contraction is
// computed by inclusion-exclusion over shared cards (three_way_deal_values),
// and the showdown tensors are contracted in interleaved partial sums
// (contract_interleaved).
//
// Each traversal first collects the reach at every terminal, then contracts
// all terminals (in parallel with more than one thread: the contractions are
// independent and each is computed in one piece, so the values do not depend
// on the thread count), then walks the tree with those values.
#include "monker_chart_format.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gtosd::checkdown_classes {

namespace ca = card_abstraction;
namespace pb = preflop_blueprint;

inline constexpr std::size_t class_count = 81U;
inline constexpr std::size_t maximum_seats = 3U;
inline constexpr double units_per_ante = static_cast<double>(Money::units_per_ante);
// Mean own reach below which a hand class counts as outside the acting range
// (it prints as 0.000 in the chart format), as in the combo-level program.
inline constexpr double out_of_range_reach = 5e-4;
// Ordered deals of distinct disjoint combos: 630 x C(34,2) heads-up, times
// C(32,2) for a third seat.
inline constexpr double heads_up_deals = 630.0 * 561.0;
inline constexpr double three_way_deals = 630.0 * 561.0 * 496.0;

// Combos per hand class: 6 for a pair, 4 suited, 12 offsuit.
inline std::array<double, class_count> class_masses() {
  std::array<double, class_count> masses{};
  for (const auto hand_class : ca::combo_table().hand_class) {
    masses[hand_class] += 1.0;
  }
  return masses;
}

// Heads-up outcomes of one hero combo against one opponent class: the
// opponent's combos disjoint from the hero and their summed wins, ties and
// losses (the hero's view) over the 201,376 runouts each.
struct ClassOutcome {
  std::uint32_t opponents{0U};
  std::uint64_t wins{0U};
  std::uint64_t ties{0U};
  std::uint64_t losses{0U};
  friend constexpr bool operator==(const ClassOutcome &, const ClassOutcome &) = default;
};
using ClassOutcomeRow = std::array<ClassOutcome, class_count>;

// Row of any hero combo, per opponent class, from preflop_all_in_v1.bin.
inline ClassOutcomeRow heads_up_row(const ca::AllInTable &table, const std::uint16_t hero_combo) {
  const auto &combos = ca::combo_table();
  ClassOutcomeRow row{};
  for (std::uint16_t opponent = 0U; opponent < ca::combo_count; ++opponent) {
    if ((combos.masks[hero_combo] & combos.masks[opponent]) != 0U) {
      continue;
    }
    const auto outcome = table.outcome(hero_combo, opponent);
    if (outcome.total() != ca::all_in_runout_count) {
      throw std::runtime_error("all-in table has no full runout count for a disjoint pair");
    }
    auto &entry = row[combos.hand_class[opponent]];
    ++entry.opponents;
    entry.wins += outcome.wins;
    entry.ties += outcome.ties;
    entry.losses += outcome.losses;
  }
  return row;
}

// preflop_all_in_v1.bin aggregated to 81 x 81: row H is the row of the
// representative of class H. Every combo of H has the same row (suit
// symmetry; the tests check all 630 combos), so the aggregation is exact.
inline std::vector<ClassOutcomeRow> heads_up_class_outcomes(const ca::AllInTable &table) {
  std::vector<ClassOutcomeRow> rows(class_count);
  for (std::size_t hero = 0; hero < class_count; ++hero) {
    rows[hero] =
        heads_up_row(table, ca::ThreeWayTable::representative_of(static_cast<std::uint8_t>(hero)));
  }
  return rows;
}

// Reach of every seat's own actions per hand class, seat major.
using Reach = std::vector<std::vector<double>>;

// Outer product of the reach of every seat except `hero`, in seat order with
// the first other seat major: the opponent's reach heads-up, 6,561 entries
// for 3 seats (index A * 81 + B for the classes A and B of the lower and the
// higher other seat). The terminal tensors use the same layout.
inline std::vector<double> others_outer(const Reach &reach, const std::uint8_t hero) {
  std::vector<double> outer{1.0};
  for (std::size_t seat = 0; seat < reach.size(); ++seat) {
    if (seat == hero) {
      continue;
    }
    std::vector<double> next(outer.size() * class_count, 0.0);
    for (std::size_t index = 0; index < outer.size(); ++index) {
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        next[index * class_count + hand_class] = outer[index] * reach[seat][hand_class];
      }
    }
    outer = std::move(next);
  }
  return outer;
}

// The lower and the higher seat other than `hero` among 3.
inline std::array<std::uint8_t, 2> other_seats(const std::uint8_t hero) {
  return {static_cast<std::uint8_t>(hero == 0U ? 1U : 0U),
          static_cast<std::uint8_t>(hero == 2U ? 1U : 2U)};
}

// Hand class of the combo of two distinct cards.
inline const std::array<std::array<std::uint8_t, ca::deck_cards>, ca::deck_cards> &
card_pair_classes() {
  static const auto classes = [] {
    std::array<std::array<std::uint8_t, ca::deck_cards>, ca::deck_cards> table{};
    const auto &combos = ca::combo_table();
    for (std::size_t combo = 0; combo < ca::combo_count; ++combo) {
      const auto &cards = combos.cards[combo];
      table[cards[0]][cards[1]] = combos.hand_class[combo];
      table[cards[1]][cards[0]] = combos.hand_class[combo];
    }
    return table;
  }();
  return classes;
}

// The 3-seat deal count tensor contracted with the other seats' reach:
// values[H] = sum over the combos a of the lower other seat and b of the
// higher one, disjoint from the representative h of H and from each other, of
// reach[lower][class a] * reach[higher][class b]. By inclusion-exclusion over
// shared cards, with S the reach sums over the combos disjoint from h, C(c)
// the reach of those holding card c and P the product of both reaches on the
// same combo:
//   values[H] = S_1 S_2 - sum_{c not in h} C_1(c) C_2(c) + P,
// since the b that share a card with a are those holding a's first or second
// card, a itself counted twice. About 5,000 operations instead of the
// 531,441 of the dense contraction, equal to it up to rounding.
inline std::vector<double> three_way_deal_values(const Reach &reach, const std::uint8_t hero) {
  const auto seats = other_seats(hero);
  const auto &first = reach[seats[0]];
  const auto &second = reach[seats[1]];
  const auto &combos = ca::combo_table();
  const auto &pair_class = card_pair_classes();
  std::array<double, ca::deck_cards> first_by_card{};
  std::array<double, ca::deck_cards> second_by_card{};
  std::array<double, ca::deck_cards> both_by_card{};
  double first_total = 0.0;
  double second_total = 0.0;
  double both_total = 0.0;
  for (std::size_t combo = 0; combo < ca::combo_count; ++combo) {
    const auto hand_class = combos.hand_class[combo];
    const double a = first[hand_class];
    const double b = second[hand_class];
    first_total += a;
    second_total += b;
    both_total += a * b;
    for (const auto card : combos.cards[combo]) {
      first_by_card[card] += a;
      second_by_card[card] += b;
      both_by_card[card] += a * b;
    }
  }
  std::vector<double> values(class_count, 0.0);
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    const auto &cards =
        combos.cards[ca::ThreeWayTable::representative_of(static_cast<std::uint8_t>(hand_class))];
    const auto x = cards[0];
    const auto y = cards[1];
    const double a_h = first[hand_class];
    const double b_h = second[hand_class];
    // Combos holding x or y: those of each card, h itself counted twice.
    const double first_live = first_total - first_by_card[x] - first_by_card[y] + a_h;
    const double second_live = second_total - second_by_card[x] - second_by_card[y] + b_h;
    const double both_live = both_total - both_by_card[x] - both_by_card[y] + a_h * b_h;
    double shared = 0.0;
    for (std::uint8_t card = 0U; card < ca::deck_cards; ++card) {
      if (card == x || card == y) {
        continue;
      }
      const auto with_x = pair_class[card][x];
      const auto with_y = pair_class[card][y];
      shared += (first_by_card[card] - first[with_x] - first[with_y]) *
                (second_by_card[card] - second[with_x] - second[with_y]);
    }
    values[hand_class] = first_live * second_live - shared + both_live;
  }
  return values;
}

// values[H] = scale * sum_j tensor[H * stride + j] * outer[j], with the
// stride the size of the outer product.
inline std::vector<double> contract(const std::vector<double> &tensor, const double scale,
                                    const std::vector<double> &outer) {
  const auto stride = outer.size();
  if (tensor.size() != class_count * stride) {
    throw std::logic_error("terminal tensor and reach product differ in size");
  }
  std::vector<double> values(class_count, 0.0);
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    const double *row = tensor.data() + hand_class * stride;
    double sum = 0.0;
    for (std::size_t index = 0; index < stride; ++index) {
      sum += row[index] * outer[index];
    }
    values[hand_class] = scale * sum;
  }
  return values;
}

// contract in four interleaved partial sums: the same sum rounded in another
// order, about three times faster where the dependent additions of one sum
// limit the loop (the 3-seat tensors; heads-up keeps contract).
inline std::vector<double> contract_interleaved(const std::vector<double> &tensor,
                                                const double scale,
                                                const std::vector<double> &outer) {
  const auto stride = outer.size();
  if (tensor.size() != class_count * stride) {
    throw std::logic_error("terminal tensor and reach product differ in size");
  }
  std::vector<double> values(class_count, 0.0);
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    const double *row = tensor.data() + hand_class * stride;
    std::array<double, 4> sums{};
    std::size_t index = 0;
    for (; index + 4U <= stride; index += 4U) {
      sums[0] += row[index] * outer[index];
      sums[1] += row[index + 1U] * outer[index + 1U];
      sums[2] += row[index + 2U] * outer[index + 2U];
      sums[3] += row[index + 3U] * outer[index + 3U];
    }
    for (; index < stride; ++index) {
      sums[0] += row[index] * outer[index];
    }
    values[hand_class] = scale * ((sums[0] + sums[1]) + (sums[2] + sums[3]));
  }
  return values;
}

// Values of one seat at one terminal: tensors[tensor] times scale.
struct TerminalTerm {
  std::uint32_t tensor{0U};
  double scale{0.0};
};

struct CheckdownTerminals {
  std::size_t seats{0U};
  // tensors[0] is the deal count tensor (D or N), the others belong to one
  // seat at one showdown each.
  std::vector<std::vector<double>> tensors;
  // Per node id (terminals only) and seat.
  std::vector<std::array<TerminalTerm, maximum_seats>> terms;
  // Ordered deals: sum over H of |H| times the row sum of tensors[0].
  double deals{0.0};
  std::uint64_t fold_terminals{0U};
  std::uint64_t showdown_terminals{0U};

  [[nodiscard]] std::uint64_t tensor_bytes() const noexcept {
    std::uint64_t bytes = 0U;
    for (const auto &tensor : tensors) {
      bytes += tensor.size() * sizeof(double);
    }
    return bytes;
  }
};

// Sum over H of |H| times the row sum of a count tensor.
inline double deal_count(const std::vector<double> &counts) {
  const auto masses = class_masses();
  const auto stride = counts.size() / class_count;
  double deals = 0.0;
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    double row = 0.0;
    for (std::size_t index = 0; index < stride; ++index) {
      row += counts[hand_class * stride + index];
    }
    deals += masses[hand_class] * row;
  }
  return deals;
}

// The payoffs of every terminal sum to minus its rake (zero without rake),
// at every winner subset of a showdown's active players. Every node of the
// checkdown tree is preflop: a fold ends the hand before the flop, while an
// all-in runout and a checkdown leaf see the flop.
inline void check_terminal_rake(const pb::CompiledGame &game) {
  const auto &rake = game.config().rake;
  const auto seats = static_cast<std::size_t>(game.config().player_count);
  const auto sum = [seats](const std::span<const std::int64_t> payoffs) {
    std::int64_t total = 0;
    for (std::size_t seat = 0; seat < seats; ++seat) {
      total += payoffs[seat];
    }
    return total;
  };
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    const auto expected = calculate_rake(rake, game.states()[node.id].pot,
                                         node.kind == pb::NodeKind::TerminalShowdown);
    if (!expected) {
      throw std::runtime_error("rake computation failed");
    }
    const auto minus_rake = -expected.value().units();
    if (node.kind == pb::NodeKind::TerminalFold) {
      if (sum(game.fold_payoffs(node.id)) != minus_rake) {
        throw std::runtime_error("fold payoffs do not sum to minus the rake");
      }
      continue;
    }
    for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
      if ((winners & static_cast<std::uint8_t>(~node.active_mask)) != 0U) {
        continue;
      }
      if (sum(game.showdown_payoffs(node.id, winners)) != minus_rake) {
        throw std::runtime_error("showdown payoffs do not sum to minus the rake");
      }
    }
  }
}

// Heads-up terminals from preflop_all_in_v1.bin. A showdown (a preflop
// all-in or a checkdown leaf, five cards to come) gives each seat the tensor
// (W * win + T * tie + L * lose) / 201,376 over (own class, opponent class),
// with W, T and L the aggregated counts and win, tie and lose the seat's
// payoff rows for the winner sets {seat}, {both} and {other}: the combo-level
// payoff matrix of preflop_blueprint_checkdown.cpp summed over the opponent
// class.
inline CheckdownTerminals heads_up_terminals(const pb::CompiledGame &game,
                                             const ca::AllInTable &table) {
  if (game.config().player_count != 2U) {
    throw std::runtime_error("heads-up terminals need a 2-player game");
  }
  const auto rows = heads_up_class_outcomes(table);
  CheckdownTerminals terminals;
  terminals.seats = 2U;
  terminals.terms.assign(game.nodes().size(), {});
  std::vector<double> counts(class_count * class_count, 0.0);
  for (std::size_t hero = 0; hero < class_count; ++hero) {
    for (std::size_t opponent = 0; opponent < class_count; ++opponent) {
      counts[hero * class_count + opponent] = static_cast<double>(rows[hero][opponent].opponents);
    }
  }
  terminals.tensors.push_back(std::move(counts));
  constexpr auto runouts = static_cast<double>(ca::all_in_runout_count);
  for (const auto &node : game.nodes()) {
    if (node.kind == pb::NodeKind::TerminalFold) {
      const auto payoffs = game.fold_payoffs(node.id);
      for (std::size_t seat = 0; seat < 2U; ++seat) {
        terminals.terms[node.id][seat] = {0U, static_cast<double>(payoffs[seat]) / units_per_ante};
      }
      ++terminals.fold_terminals;
      continue;
    }
    if (node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    if (node.active_mask != 3U || node.remaining_board_cards != 5U) {
      throw std::runtime_error(
          "a heads-up checkdown showdown has both seats and five cards to come");
    }
    for (std::uint8_t seat = 0; seat < 2U; ++seat) {
      const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
      const auto other_bit = static_cast<std::uint8_t>(node.active_mask & ~seat_bit);
      const double win =
          static_cast<double>(game.showdown_payoffs(node.id, seat_bit)[seat]) / units_per_ante;
      const double tie =
          static_cast<double>(game.showdown_payoffs(node.id, node.active_mask)[seat]) /
          units_per_ante;
      const double lose =
          static_cast<double>(game.showdown_payoffs(node.id, other_bit)[seat]) / units_per_ante;
      std::vector<double> tensor(class_count * class_count, 0.0);
      for (std::size_t hero = 0; hero < class_count; ++hero) {
        for (std::size_t opponent = 0; opponent < class_count; ++opponent) {
          const auto &entry = rows[hero][opponent];
          tensor[hero * class_count + opponent] =
              (static_cast<double>(entry.wins) * win + static_cast<double>(entry.ties) * tie +
               static_cast<double>(entry.losses) * lose) /
              runouts;
        }
      }
      terminals.terms[node.id][seat] = {static_cast<std::uint32_t>(terminals.tensors.size()), 1.0};
      terminals.tensors.push_back(std::move(tensor));
    }
    ++terminals.showdown_terminals;
  }
  terminals.deals = deal_count(terminals.tensors[0]);
  if (terminals.deals != heads_up_deals) {
    throw std::runtime_error("the heads-up deal count is not 630 x 561");
  }
  return terminals;
}

// Payoff of a seat for a winner set, in antes.
inline double payoff_antes(const pb::CompiledGame &game, const std::uint32_t node,
                           const std::uint8_t winners, const std::uint8_t seat) {
  return static_cast<double>(game.showdown_payoffs(node, winners)[seat]) / units_per_ante;
}

// 3-way terminals from the three-player class table, in the layout
// [H][A][B] with A the class of the lower other seat and B of the higher one.
//  - 3-active showdown (a 3-way all-in or checkdown leaf): the seat's tensor
//    is the runout-weighted masses of entry (H, A, B) (three_way_mass, whose
//    first opponent is the lower other seat) times the seat's payoff for the
//    winner set: the seat alone, with the lower seat, with the higher seat,
//    all three, or the seat not among the winners. The last is one payoff,
//    checked equal over the three winner sets without the seat (one pot,
//    equal stacks); every other payoff is read per set and per seat, never
//    derived from another seat's (odd chips go to the lowest seats).
//  - 2-active showdown after a fold: each active seat's win, tie and loss
//    masses against the other active seat (two_way_mass: entry (H, opponent,
//    folded) with the folded hand's cards dead, or the heads-up table
//    weighted by the folded hand's disjoint combos when they are ignored),
//    times its payoffs for {seat}, {both} and {opponent}. The mass of (H,
//    opponent, folded) sits at [H][opponent][folded] when the opponent is the
//    lower other seat and is transposed to [H][folded][opponent] when the
//    folder is. The folder's payoff is checked equal over the winner sets
//    and uses N.
//  - Fold: every seat uses N scaled by its payoff.
// A table built for a subset of hero classes (tests) leaves the rows of the
// other classes zero and skips the deal count check.
inline CheckdownTerminals three_way_terminals(const pb::CompiledGame &game,
                                              const ca::ThreeWayTable &table,
                                              const ca::AllInTable &heads_up,
                                              const ca::FoldedCards folded_cards) {
  if (game.config().player_count != 3U) {
    throw std::runtime_error("3-way terminals need a 3-player game");
  }
  constexpr std::size_t entries = class_count * class_count * class_count;
  // Masses of every entry (H, first, second): the five winner sets of a
  // 3-active showdown, then win, tie and loss against the first opponent
  // with the second one folded.
  std::array<std::vector<double>, 5> three;
  std::array<std::vector<double>, 3> two;
  for (auto &masses : three) {
    masses.assign(entries, 0.0);
  }
  for (auto &masses : two) {
    masses.assign(entries, 0.0);
  }
  std::vector<double> counts(entries, 0.0);
  for (std::uint8_t hero = 0U; hero < class_count; ++hero) {
    for (std::uint8_t first = 0U; first < class_count; ++first) {
      for (std::uint8_t second = 0U; second < class_count; ++second) {
        const auto index = ca::ThreeWayTable::entry_index(hero, first, second);
        counts[index] = static_cast<double>(table.entry(hero, first, second).pairs);
        const auto mass = table.three_way_mass(hero, first, second);
        three[0][index] = mass.hero_alone;
        three[1][index] = mass.with_first;
        three[2][index] = mass.with_second;
        three[3][index] = mass.all_three;
        three[4][index] = mass.hero_loses;
        const auto versus = ca::two_way_mass(table, heads_up, folded_cards, hero, first, second);
        two[0][index] = versus.wins;
        two[1][index] = versus.ties;
        two[2][index] = versus.losses;
      }
    }
  }
  CheckdownTerminals terminals;
  terminals.seats = 3U;
  terminals.terms.assign(game.nodes().size(), {});
  terminals.tensors.push_back(std::move(counts));
  const auto add_tensor = [&terminals](const std::uint32_t node, const std::uint8_t seat,
                                       std::vector<double> tensor) {
    terminals.terms[node][seat] = {static_cast<std::uint32_t>(terminals.tensors.size()), 1.0};
    terminals.tensors.push_back(std::move(tensor));
  };
  for (const auto &node : game.nodes()) {
    if (node.kind == pb::NodeKind::TerminalFold) {
      const auto payoffs = game.fold_payoffs(node.id);
      for (std::size_t seat = 0; seat < 3U; ++seat) {
        terminals.terms[node.id][seat] = {0U, static_cast<double>(payoffs[seat]) / units_per_ante};
      }
      ++terminals.fold_terminals;
      continue;
    }
    if (node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    if (node.remaining_board_cards != 5U) {
      throw std::runtime_error("a 3-way checkdown showdown has five cards to come");
    }
    if (node.active_mask == 7U) {
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        const auto seats = other_seats(seat);
        const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
        const auto lower_bit = static_cast<std::uint8_t>(1U << seats[0]);
        const auto higher_bit = static_cast<std::uint8_t>(1U << seats[1]);
        const double lose = payoff_antes(game, node.id, lower_bit, seat);
        if (payoff_antes(game, node.id, higher_bit, seat) != lose ||
            payoff_antes(game, node.id, static_cast<std::uint8_t>(lower_bit | higher_bit), seat) !=
                lose) {
          throw std::runtime_error(
              "a seat outside the winners of a 3-way showdown has more than one payoff");
        }
        const std::array<double, 5> payoffs{
            payoff_antes(game, node.id, seat_bit, seat),
            payoff_antes(game, node.id, static_cast<std::uint8_t>(seat_bit | lower_bit), seat),
            payoff_antes(game, node.id, static_cast<std::uint8_t>(seat_bit | higher_bit), seat),
            payoff_antes(game, node.id, 7U, seat), lose};
        std::vector<double> tensor(entries, 0.0);
        for (std::size_t index = 0; index < entries; ++index) {
          double value = 0.0;
          for (std::size_t outcome = 0; outcome < payoffs.size(); ++outcome) {
            value += three[outcome][index] * payoffs[outcome];
          }
          tensor[index] = value;
        }
        add_tensor(node.id, seat, std::move(tensor));
      }
    } else if (std::popcount(node.active_mask) == 2 && node.active_mask < 8U) {
      const auto folded = static_cast<std::uint8_t>(
          std::countr_zero(static_cast<unsigned>(~node.active_mask & 7U)));
      const double folded_payoff = payoff_antes(game, node.id, node.active_mask, folded);
      for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
        if ((winners & static_cast<std::uint8_t>(~node.active_mask)) == 0U &&
            payoff_antes(game, node.id, winners, folded) != folded_payoff) {
          throw std::runtime_error("a folded seat has more than one payoff at a showdown");
        }
      }
      terminals.terms[node.id][folded] = {0U, folded_payoff};
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        if (seat == folded) {
          continue;
        }
        const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
        const auto opponent_bit = static_cast<std::uint8_t>(node.active_mask & ~seat_bit);
        const auto opponent = static_cast<std::uint8_t>(std::countr_zero(opponent_bit));
        const std::array<double, 3> payoffs{payoff_antes(game, node.id, seat_bit, seat),
                                            payoff_antes(game, node.id, node.active_mask, seat),
                                            payoff_antes(game, node.id, opponent_bit, seat)};
        const bool transposed = folded < opponent;
        std::vector<double> tensor(entries, 0.0);
        for (std::uint8_t hero = 0U; hero < class_count; ++hero) {
          for (std::uint8_t lower = 0U; lower < class_count; ++lower) {
            for (std::uint8_t higher = 0U; higher < class_count; ++higher) {
              // (H, opponent's class, folder's class)
              const auto source = transposed ? ca::ThreeWayTable::entry_index(hero, higher, lower)
                                             : ca::ThreeWayTable::entry_index(hero, lower, higher);
              double value = 0.0;
              for (std::size_t outcome = 0; outcome < payoffs.size(); ++outcome) {
                value += two[outcome][source] * payoffs[outcome];
              }
              tensor[ca::ThreeWayTable::entry_index(hero, lower, higher)] = value;
            }
          }
        }
        add_tensor(node.id, seat, std::move(tensor));
      }
    } else {
      throw std::runtime_error("a 3-way checkdown showdown has 2 or 3 active seats");
    }
    ++terminals.showdown_terminals;
  }
  terminals.deals = deal_count(terminals.tensors[0]);
  if (table.complete() && terminals.deals != three_way_deals) {
    throw std::runtime_error("the 3-way deal count is not 630 x 561 x 496");
  }
  return terminals;
}

enum class Mode : std::uint8_t { Update, Average, BestResponse };

// Vector DCFR over the 81 classes with full traversals, alternating updates
// over every seat and the discounts of preflop_blueprint_checkdown.cpp;
// exact values and best response of the average profile. Rows can be locked
// to given frequencies (a preflop lock from charts): a locked row is the
// current and the average strategy, the updates skip it, and the best
// response still takes the best action there.
class ClassSolver {
public:
  ClassSolver(const pb::CompiledGame &game, const CheckdownTerminals &terminals, const double alpha,
              const double beta, const double gamma, const unsigned threads = 1U)
      : game_(game), terminals_(terminals), masses_(class_masses()), alpha_(alpha), beta_(beta),
        gamma_(gamma), threads_(std::max(1U, threads)) {
    const auto &nodes = game_.nodes();
    if (terminals_.seats != game_.config().player_count || terminals_.seats < 2U ||
        terminals_.seats > maximum_seats || terminals_.terms.size() != nodes.size()) {
      throw std::runtime_error("the terminal tensors do not belong to this game");
    }
    offsets_.assign(nodes.size(), 0U);
    std::size_t cells = 0;
    for (const auto &node : nodes) {
      if (node.kind == pb::NodeKind::Chance) {
        throw std::runtime_error("the checkdown tree still has a chance node");
      }
      if (node.street != Street::Preflop) {
        throw std::runtime_error("the checkdown tree still has a postflop node");
      }
      if (node.kind == pb::NodeKind::Decision) {
        offsets_[node.id] = cells;
        cells += class_count * node.action_count;
      } else {
        terminal_nodes_.push_back(node.id);
      }
    }
    regret_.assign(cells, 0.0);
    strategy_sum_.assign(cells, 0.0);
    locked_.assign(cells, 0U);
    lock_.assign(cells, 0.0);
    terminal_reach_.assign(nodes.size(), Reach{});
    terminal_values_.assign(nodes.size(), std::vector<double>{});
    local_gain_.assign(nodes.size(), 0.0);
  }

  [[nodiscard]] std::size_t seats() const noexcept { return terminals_.seats; }

  // Fixes the row of a class at a decision node, in edge order; the row must
  // be a probability distribution.
  void lock_row(const std::uint32_t node_id, const std::size_t hand_class,
                const std::vector<double> &frequencies) {
    const auto &node = game_.nodes().at(node_id);
    if (node.kind != pb::NodeKind::Decision || hand_class >= class_count ||
        frequencies.size() != node.action_count) {
      throw std::runtime_error("a locked row does not fit its node");
    }
    double total = 0.0;
    for (const auto frequency : frequencies) {
      if (!(frequency >= 0.0)) {
        throw std::runtime_error("a locked row has a negative frequency");
      }
      total += frequency;
    }
    if (std::abs(total - 1.0) > 1e-9) {
      throw std::runtime_error("a locked row does not sum to one");
    }
    const auto base = offsets_[node_id] + hand_class * frequencies.size();
    locked_rows_ += locked_[base] == 0U ? 1U : 0U;
    locked_[base] = 1U;
    std::copy(frequencies.begin(), frequencies.end(),
              lock_.begin() + static_cast<std::ptrdiff_t>(base));
  }

  [[nodiscard]] std::size_t locked_rows() const noexcept { return locked_rows_; }

  // One DCFR iteration: every seat in turn (alternating), then the discounts.
  void iterate(const std::uint64_t iteration) {
    const auto ones = uniform_reach();
    for (std::size_t hero = 0; hero < seats(); ++hero) {
      prepare_terminals(static_cast<std::uint8_t>(hero), false);
      (void)traverse(game_.root(), static_cast<std::uint8_t>(hero), ones, Mode::Update);
    }
    const double t = static_cast<double>(iteration);
    const double positive = std::pow(t, alpha_) / (std::pow(t, alpha_) + 1.0);
    const double negative = std::pow(t, beta_) / (std::pow(t, beta_) + 1.0);
    const double average = std::pow(t / (t + 1.0), gamma_);
    for (std::size_t cell = 0; cell < regret_.size(); ++cell) {
      regret_[cell] *= regret_[cell] > 0.0 ? positive : negative;
      strategy_sum_[cell] *= average;
    }
  }

  // Expected value of a seat per hand, in antes, when the seat plays `mode`
  // (average strategy or best response) against the others' average
  // strategies. The average also sets the local gains of the seat's nodes.
  double value(const std::uint8_t hero, const Mode mode) {
    prepare_terminals(hero, true);
    const auto values = traverse(game_.root(), hero, uniform_reach(), mode);
    double sum = 0.0;
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      sum += masses_[hand_class] * values[hand_class];
    }
    return sum / terminals_.deals;
  }

  // What the actor of a decision node gains per hand, in antes, by playing
  // the best action of every class there and the average strategy elsewhere
  // (the other seats on their average): the sum over classes of own reach
  // times the best action value minus the node value. Set by the last
  // value(actor, Mode::Average).
  [[nodiscard]] double local_gain(const std::uint32_t node) const { return local_gain_.at(node); }

  // Average strategy of a class at a decision node, in edge order.
  [[nodiscard]] std::vector<double> average_strategy(const std::uint32_t node,
                                                     const std::size_t hand_class) const {
    std::vector<double> row(game_.nodes()[node].action_count, 0.0);
    fill_strategy(node, hand_class, true, row.data());
    return row;
  }

  // Current strategy (regret matching, uniform without a positive regret) of a
  // class at a decision node, in edge order; the row itself where it is locked
  // (phase 3, V9: the step-2 trainer's current policy against this one).
  [[nodiscard]] std::vector<double> current_strategy(const std::uint32_t node,
                                                     const std::size_t hand_class) const {
    std::vector<double> row(game_.nodes()[node].action_count, 0.0);
    fill_strategy(node, hand_class, false, row.data());
    return row;
  }

  // Reach of every seat's own actions under the average profile, per node.
  [[nodiscard]] std::vector<Reach> own_reach() const {
    std::vector<Reach> reach(game_.nodes().size());
    forward(game_.root(), uniform_reach(), reach);
    return reach;
  }

private:
  [[nodiscard]] Reach uniform_reach() const {
    return Reach(seats(), std::vector<double>(class_count, 1.0));
  }

  // Current (regret matching) or average strategy of a class at a node;
  // uniform where the sum is zero; the row itself where it is locked.
  void fill_strategy(const std::uint32_t node, const std::size_t hand_class, const bool use_average,
                     double *row) const {
    const auto actions = static_cast<std::size_t>(game_.nodes()[node].action_count);
    const auto &source = use_average ? strategy_sum_ : regret_;
    const auto base = offsets_[node] + hand_class * actions;
    if (locked_[base] != 0U) {
      for (std::size_t action = 0; action < actions; ++action) {
        row[action] = lock_[base + action];
      }
      return;
    }
    double total = 0.0;
    for (std::size_t action = 0; action < actions; ++action) {
      const double entry =
          use_average ? source[base + action] : std::max(source[base + action], 0.0);
      row[action] = entry;
      total += entry;
    }
    for (std::size_t action = 0; action < actions; ++action) {
      row[action] = total > 0.0 ? row[action] / total : 1.0 / static_cast<double>(actions);
    }
  }

  // The strategies of every class at a node, class major.
  [[nodiscard]] std::vector<double> strategy_table(const std::uint32_t node,
                                                   const bool use_average) const {
    const auto actions = static_cast<std::size_t>(game_.nodes()[node].action_count);
    std::vector<double> result(class_count * actions, 0.0);
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      fill_strategy(node, hand_class, use_average, result.data() + hand_class * actions);
    }
    return result;
  }

  // Values of a seat at a terminal from the reach collected there.
  [[nodiscard]] std::vector<double> terminal_values(const std::uint32_t node_id,
                                                    const std::uint8_t hero) const {
    const auto &term = terminals_.terms[node_id][hero];
    const auto &reach = terminal_reach_[node_id];
    if (seats() == 3U) {
      if (term.tensor == 0U) {
        auto values = three_way_deal_values(reach, hero);
        for (auto &value : values) {
          value *= term.scale;
        }
        return values;
      }
      return contract_interleaved(terminals_.tensors[term.tensor], term.scale,
                                  others_outer(reach, hero));
    }
    return contract(terminals_.tensors[term.tensor], term.scale, others_outer(reach, hero));
  }

  // The reach of every seat at every terminal under the current (update) or
  // average strategies, multiplied as traverse multiplies it.
  void collect(const std::uint32_t node_id, const Reach &reach, const bool use_average) {
    const auto &node = game_.nodes()[node_id];
    if (node.kind != pb::NodeKind::Decision) {
      terminal_reach_[node_id] = reach;
      return;
    }
    const auto edges = game_.edges_of(node_id);
    const auto actions = edges.size();
    const auto strategies = strategy_table(node_id, use_average);
    for (std::size_t action = 0; action < actions; ++action) {
      auto next = reach;
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        next[node.actor][hand_class] *= strategies[hand_class * actions + action];
      }
      collect(edges[action].child, next, use_average);
    }
  }

  // The values of `hero` at every terminal for the traversal that follows,
  // spread over the threads (one terminal per task).
  void prepare_terminals(const std::uint8_t hero, const bool use_average) {
    collect(game_.root(), uniform_reach(), use_average);
    const auto count = terminal_nodes_.size();
    if (threads_ == 1U) {
      for (const auto node : terminal_nodes_) {
        terminal_values_[node] = terminal_values(node, hero);
      }
      return;
    }
    std::atomic<std::size_t> next{0U};
    std::atomic<bool> failed{false};
    std::exception_ptr failure;
    std::mutex failure_mutex;
    const auto work = [&] {
      while (!failed.load(std::memory_order_relaxed)) {
        const auto index = next.fetch_add(1U, std::memory_order_relaxed);
        if (index >= count) {
          return;
        }
        try {
          const auto node = terminal_nodes_[index];
          terminal_values_[node] = terminal_values(node, hero);
        } catch (...) {
          const std::scoped_lock lock(failure_mutex);
          if (!failure) {
            failure = std::current_exception();
          }
          failed.store(true, std::memory_order_relaxed);
          return;
        }
      }
    };
    {
      std::vector<std::jthread> workers;
      workers.reserve(threads_ - 1U);
      for (unsigned worker = 1U; worker < threads_; ++worker) {
        workers.emplace_back(work);
      }
      work();
    }
    if (failure) {
      std::rethrow_exception(failure);
    }
  }

  std::vector<double> traverse(const std::uint32_t node_id, const std::uint8_t hero,
                               const Reach &reach, const Mode mode) {
    const auto &node = game_.nodes()[node_id];
    if (node.kind == pb::NodeKind::TerminalFold || node.kind == pb::NodeKind::TerminalShowdown) {
      return terminal_values_[node_id];
    }
    const auto edges = game_.edges_of(node_id);
    const auto actions = edges.size();
    const auto strategies = strategy_table(node_id, mode != Mode::Update);
    std::vector<double> values(class_count, 0.0);
    if (node.actor != hero) {
      for (std::size_t action = 0; action < actions; ++action) {
        auto next = reach;
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          next[node.actor][hand_class] *= strategies[hand_class * actions + action];
        }
        const auto child = traverse(edges[action].child, hero, next, mode);
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          values[hand_class] += child[hand_class];
        }
      }
      return values;
    }
    std::vector<std::vector<double>> children(actions);
    for (std::size_t action = 0; action < actions; ++action) {
      auto next = reach;
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        next[hero][hand_class] *= strategies[hand_class * actions + action];
      }
      children[action] = traverse(edges[action].child, hero, next, mode);
    }
    double gain = 0.0;
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      double best = children[0][hand_class];
      for (std::size_t action = 1; action < actions; ++action) {
        best = std::max(best, children[action][hand_class]);
      }
      if (mode == Mode::BestResponse) {
        values[hand_class] = best;
        continue;
      }
      const double *strategy = strategies.data() + hand_class * actions;
      double node_value = 0.0;
      for (std::size_t action = 0; action < actions; ++action) {
        node_value += strategy[action] * children[action][hand_class];
      }
      values[hand_class] = node_value;
      const auto base = offsets_[node_id] + hand_class * actions;
      if (mode == Mode::Average) {
        gain += masses_[hand_class] * reach[hero][hand_class] * (best - node_value);
      } else if (locked_[base] == 0U) {
        for (std::size_t action = 0; action < actions; ++action) {
          regret_[base + action] += children[action][hand_class] - node_value;
          strategy_sum_[base + action] += reach[hero][hand_class] * strategy[action];
        }
      }
    }
    if (mode == Mode::Average) {
      local_gain_[node_id] = gain / terminals_.deals;
    }
    return values;
  }

  void forward(const std::uint32_t node_id, const Reach &reach, std::vector<Reach> &output) const {
    output[node_id] = reach;
    const auto &node = game_.nodes()[node_id];
    if (node.kind != pb::NodeKind::Decision) {
      return;
    }
    const auto edges = game_.edges_of(node_id);
    const auto actions = edges.size();
    const auto strategies = strategy_table(node_id, true);
    for (std::size_t action = 0; action < actions; ++action) {
      auto next = reach;
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        next[node.actor][hand_class] *= strategies[hand_class * actions + action];
      }
      forward(edges[action].child, next, output);
    }
  }

  const pb::CompiledGame &game_;
  const CheckdownTerminals &terminals_;
  std::array<double, class_count> masses_;
  double alpha_;
  double beta_;
  double gamma_;
  unsigned threads_;
  std::vector<std::size_t> offsets_;
  std::vector<double> regret_;
  std::vector<double> strategy_sum_;
  // Per cell: whether the row starting there is locked, and its frequencies.
  std::vector<std::uint8_t> locked_;
  std::vector<double> lock_;
  std::size_t locked_rows_{0U};
  std::vector<std::uint32_t> terminal_nodes_;
  // Per node id (terminals only), for the current traversal.
  std::vector<Reach> terminal_reach_;
  std::vector<std::vector<double>> terminal_values_;
  std::vector<double> local_gain_;
};

// The rake of every terminal times the probability that the average profile
// reaches it (every seat's own reach over the disjoint deals of tensor 0),
// independent of the payoffs and of the value traversal: it equals minus the
// sum of the seats' EVs.
inline double expected_rake_by_reach(const pb::CompiledGame &game,
                                     const CheckdownTerminals &terminals,
                                     const std::vector<Reach> &reach) {
  const auto masses = class_masses();
  const auto &rake = game.config().rake;
  double total = 0.0;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    const auto amount = calculate_rake(rake, game.states()[node.id].pot,
                                       node.kind == pb::NodeKind::TerminalShowdown);
    if (!amount) {
      throw std::runtime_error("rake computation failed");
    }
    if (amount.value().units() == 0) {
      continue;
    }
    const auto deals = contract(terminals.tensors[0], 1.0, others_outer(reach[node.id], 0U));
    double mass = 0.0;
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      mass += masses[hand_class] * reach[node.id][0][hand_class] * deals[hand_class];
    }
    total += static_cast<double>(amount.value().units()) / units_per_ante * mass / terminals.deals;
  }
  return total;
}

// One chart per preflop decision node through mc::write_charts, which names
// the files and orders the columns by chart_nodes (MonkerSolver's fold rule
// in a multiway tree). The row of a class is its average strategy; a class
// whose own reach at the node is below out_of_range_reach is an all-zero row.
inline std::vector<std::string> write_class_charts(const pb::CompiledGame &game,
                                                   const ClassSolver &solver,
                                                   const std::filesystem::path &directory) {
  const auto classes = monker_charts::hand_classes();
  std::map<std::string, std::uint8_t> class_of_label;
  for (const auto &[hand_class, label] : classes.label_by_class) {
    class_of_label[label] = hand_class;
  }
  std::vector<std::string> labels;
  for (const auto &entry : classes.combos_by_label) {
    labels.push_back(entry.first);
  }
  const auto reach = solver.own_reach();
  const monker_charts::ClassStrategy strategy =
      [&](const std::uint32_t node_id,
          const std::string &label) -> std::optional<std::vector<double>> {
    const auto hand_class = class_of_label.at(label);
    if (reach[node_id][game.nodes()[node_id].actor][hand_class] < out_of_range_reach) {
      return std::nullopt;
    }
    return solver.average_strategy(node_id, hand_class);
  };
  return monker_charts::write_charts(game, labels, strategy, directory);
}

} // namespace gtosd::checkdown_classes
