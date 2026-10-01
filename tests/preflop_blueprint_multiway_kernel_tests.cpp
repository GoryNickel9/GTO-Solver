// Phase 3 lane K tests (spec PHASE3_SPEC_2026-09-30, sections 3.4-3.7 and 7):
//   K1  BoardContext rank-group starts and the card-pair -> live-hand index;
//   V2  the three-seat kernels against their O(n^3) references on 30 boards (tie-heavy boards
//       where the board plays, full house and quads on board, paired, flush and the A-6-7-8-9
//       straight, plus random boards), dense, sparse (20 hands per seat) and zero-laden reach,
//       every hero seat and every folder, both instruction sets, with the D-scaled tolerance
//       |x - ref| <= 1e-12 * max(1, D_ref[h]); a deliberately swapped seat pair must fail;
//   V3  a folded seat with reach 1 on every live hand: the two-active masses equal 351 times the
//       heads-up masses (showdown_masses, fold_mass);
//   V8  through the kernel API, on the three 3WAY50 rake fixtures, 20 boards, every postflop
//       terminal: sum_s <r_s, V_s> equals the rake taken per winner set through the masses, and
//       <r_s, D3_s> is equal for the three seats (tolerance relative to the sum of |terms|).
// Also: the scratch is clean after every call (a reused scratch reproduces a fresh one bit for
// bit), and the value kernels equal masses times payoffs.
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/core/money.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"
#include "gtosd/preflop_blueprint/multiway_kernels.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;
using Vector = std::array<double, pb::live_hand_count>;

constexpr double mass_tolerance = 1e-12;
constexpr double identity_tolerance = 1e-12;
constexpr double ante_scale = 1.0 / static_cast<double>(gtosd::Money::units_per_ante);

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

double seconds_since(const Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

ca::RankTable load_ranks(const std::filesystem::path &resources_dir) {
  if (!resources_dir.empty()) {
    auto loaded = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
    if (loaded) {
      std::cout << "rank table loaded from " << resources_dir.string() << '\n';
      return std::move(loaded.value());
    }
  }
  std::cout << "rank table not found under '" << resources_dir.string() << "', building it\n";
  auto built = ca::RankTable::build();
  require(built.has_value(), "rank table builds");
  return std::move(built.value());
}

ca::BoardHistory board_from_cards(std::array<std::uint8_t, 5> cards) {
  std::sort(cards.begin(), cards.begin() + 3);
  ca::BoardHistory history;
  for (std::size_t index = 0; index < 3U; ++index) {
    history.flop[index] = gtosd::CardId::from_index(cards[index]).value();
  }
  history.turn = gtosd::CardId::from_index(cards[3]).value();
  history.river = gtosd::CardId::from_index(cards[4]).value();
  return history;
}

ca::BoardHistory parse_board(const std::string_view text) {
  std::array<std::uint8_t, 5> cards{};
  std::istringstream input{std::string(text)};
  for (auto &card : cards) {
    std::string token;
    input >> token;
    const auto parsed = gtosd::parse_card(token);
    require(parsed.has_value(), "board card parses");
    card = parsed.value().value();
  }
  return board_from_cards(cards);
}

ca::BoardHistory random_board(ca::DeterministicRandom &random) {
  std::array<std::uint8_t, 5> cards{};
  std::uint64_t used = 0U;
  for (auto &card : cards) {
    do {
      card = static_cast<std::uint8_t>(random.uniform_below(36U));
    } while (((used >> card) & 1U) != 0U);
    used |= std::uint64_t{1} << card;
  }
  return board_from_cards(cards);
}

pb::BoardContext make_context(const ca::RankTable &ranks, const ca::BoardHistory &history) {
  const auto context = pb::BoardContext::build(history, ranks);
  require(context.has_value(), "board context builds");
  return context.value();
}

// Tie-heavy boards of spec V2 first, then random ones.
struct NamedBoard {
  std::string name;
  ca::BoardHistory history;
  bool special{false};
};

std::vector<NamedBoard> v2_boards(const std::size_t total) {
  std::vector<NamedBoard> boards{
      {"broadway on board (Tc Jd Qh Ks Ac)", parse_board("Tc Jd Qh Ks Ac"), true},
      {"full house on board (Kc Kd Kh 7s 7c)", parse_board("Kc Kd Kh 7s 7c"), true},
      {"quads on board (9c 9d 9h 9s Ad)", parse_board("9c 9d 9h 9s Ad"), true},
      {"paired (8c 8d Jh Qs 6c)", parse_board("8c 8d Jh Qs 6c"), true},
      {"two pair on board (Tc Td 7h 7s Kc)", parse_board("Tc Td 7h 7s Kc"), true},
      {"flush on board (6h 8h Th Qh Ah)", parse_board("6h 8h Th Qh Ah"), true},
      {"four to a flush (7s 9s Js Ks 6d)", parse_board("7s 9s Js Ks 6d"), true},
      {"A-6-7-8-9 straight (Ac 6d 7h 8s 9c)", parse_board("Ac 6d 7h 8s 9c"), true},
      {"straight flush on board (6s 7s 8s 9s Ts)", parse_board("6s 7s 8s 9s Ts"), true}};
  ca::DeterministicRandom random(0x3A11'0002ULL);
  while (boards.size() < total) {
    boards.push_back({"random " + std::to_string(boards.size()), random_board(random), false});
  }
  return boards;
}

enum class ReachMode { Dense, Sparse, Zeros };

const char *mode_name(const ReachMode mode) {
  switch (mode) {
  case ReachMode::Dense:
    return "dense";
  case ReachMode::Sparse:
    return "sparse";
  case ReachMode::Zeros:
    return "zeros";
  }
  return "?";
}

Vector random_reach(ca::DeterministicRandom &random, const ReachMode mode) {
  Vector reach{};
  if (mode == ReachMode::Sparse) {
    // About 20 hands with random positive weights, the rest exactly zero.
    for (int pick = 0; pick < 20; ++pick) {
      reach[random.uniform_below(static_cast<std::uint32_t>(pb::live_hand_count))] =
          0.05 + random.uniform_unit();
    }
    return reach;
  }
  for (auto &value : reach) {
    value = random.uniform_unit();
    if (mode == ReachMode::Zeros && random.uniform_below(2U) == 0U) {
      value = 0.0;
    }
  }
  return reach;
}

pb::ConstHandSpan view(const Vector &vector) { return pb::ConstHandSpan(vector.data(), vector.size()); }
pb::HandSpan view(Vector &vector) { return pb::HandSpan(vector.data(), vector.size()); }

struct Three {
  Vector win{};
  Vector tie_lower{};
  Vector tie_higher{};
  Vector tie_both{};
  Vector lose{};
  pb::ThreeActiveMassSpans spans() {
    return {view(win), view(tie_lower), view(tie_higher), view(tie_both), view(lose)};
  }
  [[nodiscard]] Vector deal() const {
    Vector total{};
    for (std::size_t hand = 0; hand < total.size(); ++hand) {
      total[hand] = win[hand] + tie_lower[hand] + tie_higher[hand] + tie_both[hand] + lose[hand];
    }
    return total;
  }
};

struct Two {
  Vector win{};
  Vector tie{};
  Vector lose{};
  pb::TwoActiveMassSpans spans() { return {view(win), view(tie), view(lose)}; }
  [[nodiscard]] Vector deal() const {
    Vector total{};
    for (std::size_t hand = 0; hand < total.size(); ++hand) {
      total[hand] = win[hand] + tie[hand] + lose[hand];
    }
    return total;
  }
};

// max over h of |value - reference| / max(1, deal[h]).
double scaled_error(const Vector &value, const Vector &reference, const Vector &deal) {
  double worst = 0.0;
  for (std::size_t hand = 0; hand < value.size(); ++hand) {
    worst = std::max(worst, std::abs(value[hand] - reference[hand]) / std::max(1.0, deal[hand]));
  }
  return worst;
}

double scaled_error(const Three &value, const Three &reference, const Vector &deal) {
  return std::max({scaled_error(value.win, reference.win, deal),
                   scaled_error(value.tie_lower, reference.tie_lower, deal),
                   scaled_error(value.tie_higher, reference.tie_higher, deal),
                   scaled_error(value.tie_both, reference.tie_both, deal),
                   scaled_error(value.lose, reference.lose, deal)});
}

double scaled_error(const Two &value, const Two &reference, const Vector &deal) {
  return std::max({scaled_error(value.win, reference.win, deal),
                   scaled_error(value.tie, reference.tie, deal),
                   scaled_error(value.lose, reference.lose, deal)});
}

bool same_bits(const Vector &left, const Vector &right) {
  for (std::size_t hand = 0; hand < left.size(); ++hand) {
    if (std::bit_cast<std::uint64_t>(left[hand]) != std::bit_cast<std::uint64_t>(right[hand])) {
      return false;
    }
  }
  return true;
}

std::array<std::uint8_t, 2> other_seats(const std::uint8_t hero) {
  switch (hero) {
  case 0U:
    return {1U, 2U};
  case 1U:
    return {0U, 2U};
  default:
    return {0U, 1U};
  }
}

std::vector<pb::MultiwayKernelIsa> instruction_sets() {
  std::vector<pb::MultiwayKernelIsa> sets{pb::MultiwayKernelIsa::Scalar};
  if (pb::multiway_kernel_avx2_available()) {
    sets.push_back(pb::MultiwayKernelIsa::Avx2);
  }
  return sets;
}

// ---------------------------------------------------------------------------------------- K1

void test_board_context_groups(const ca::RankTable &ranks) {
  const auto boards = v2_boards(40);
  for (const auto &board : boards) {
    const auto context = make_context(ranks, board.history);
    const auto starts = context.rank_group_starts();
    const auto order = context.order_by_rank();
    const auto hand_ranks = context.ranks();
    require(starts.size() == static_cast<std::size_t>(context.distinct_rank_groups()) + 1U &&
                static_cast<std::size_t>(starts.front()) == 0U &&
                static_cast<std::size_t>(starts.back()) == pb::live_hand_count,
            "K1: group starts span every live hand once");
    for (std::size_t group = 0; group + 1U < starts.size(); ++group) {
      require(starts[group] < starts[group + 1U], "K1: groups are non-empty");
      const auto rank = hand_ranks[order[starts[group]]];
      const std::size_t group_end = starts[group + 1U];
      for (std::size_t position = starts[group]; position < group_end; ++position) {
        require(hand_ranks[order[position]] == rank, "K1: a group holds one rank");
      }
      if (group > 0U) {
        require(hand_ranks[order[starts[group] - 1U]] < rank, "K1: groups ascend strictly");
      }
    }
    std::size_t pairs = 0U;
    for (std::uint8_t first = 0U; first < 36U; ++first) {
      for (std::uint8_t second = 0U; second < 36U; ++second) {
        const auto hand = context.pair_hand(first, second);
        if (hand == pb::no_hand) {
          continue;
        }
        ++pairs;
        const auto &cards = context.cards()[hand];
        require((cards[0] == first && cards[1] == second) ||
                    (cards[0] == second && cards[1] == first),
                "K1: the pair index returns the hand of the two cards");
      }
    }
    require(pairs == 2U * pb::live_hand_count, "K1: 930 ordered live pairs");
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      const auto &cards = context.cards()[hand];
      require(context.pair_hand(cards[0], cards[1]) == hand &&
                  context.pair_hand(cards[1], cards[0]) == hand,
              "K1: every live hand is indexed in both orders");
    }
  }
  std::cout << "K1 board context: " << boards.size()
            << " boards, group starts and pair index consistent\n";
}

// ---------------------------------------------------------------------------------------- V2

struct V2Stats {
  double worst_three{0.0};
  double worst_two{0.0};
  double worst_deal{0.0};
  double worst_isa{0.0};
  double weakest_swap_three{1e300};
  double weakest_swap_two{1e300};
  std::size_t swaps_three{0U};
  std::size_t swaps_two{0U};
  std::size_t symmetric_three{0U};
  std::size_t symmetric_two{0U};
  std::size_t reference_calls{0U};
  std::size_t kernel_calls{0U};
};

void test_v2_kernels(const ca::RankTable &ranks) {
  const auto start = Clock::now();
  const auto boards = v2_boards(30);
  const auto sets = instruction_sets();
  std::vector<pb::MultiwayScratch> scratches;
  for (const auto isa : sets) {
    scratches.emplace_back(isa);
    require(scratches.back().isa() == isa, "V2: the scratch keeps its instruction set");
  }
  ca::DeterministicRandom random(0x3A11'0003ULL);
  V2Stats stats;
  for (std::size_t index = 0; index < boards.size(); ++index) {
    const auto &board = boards[index];
    const auto context = make_context(ranks, board.history);
    std::vector<ReachMode> modes;
    if (board.special) {
      modes = {ReachMode::Dense, ReachMode::Sparse, ReachMode::Zeros};
    } else {
      modes = {static_cast<ReachMode>(index % 3U)};
    }
    for (const auto mode : modes) {
      std::array<Vector, 3> reach{random_reach(random, mode), random_reach(random, mode),
                                  random_reach(random, mode)};
      // Three active, every hero seat.
      for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
        const auto seats = other_seats(hero);
        const auto &lower = reach[seats[0]];
        const auto &higher = reach[seats[1]];
        Three reference;
        pb::three_active_masses_reference(context, view(lower), view(higher), reference.spans());
        ++stats.reference_calls;
        const auto deal = reference.deal();
        std::optional<Three> first_isa;
        for (auto &scratch : scratches) {
          Three kernel;
          pb::three_active_masses(context, view(lower), view(higher), kernel.spans(), scratch);
          ++stats.kernel_calls;
          const double error = scaled_error(kernel, reference, deal);
          stats.worst_three = std::max(stats.worst_three, error);
          require(error <= mass_tolerance,
                  "V2: three-active masses equal the reference (" + board.name + ", " +
                      mode_name(mode) + ", hero " + std::to_string(hero) + ", " +
                      pb::multiway_kernel_isa_name(scratch.isa()) + ", scaled error " +
                      std::to_string(error) + ")");
          Vector kernel_deal{};
          pb::three_seat_deal_mass(context, view(lower), view(higher), view(kernel_deal), scratch);
          const double deal_error = scaled_error(kernel_deal, deal, deal);
          stats.worst_deal = std::max(stats.worst_deal, deal_error);
          require(deal_error <= mass_tolerance, "V2: D3 equals the sum of the reference masses");
          if (first_isa) {
            stats.worst_isa = std::max(stats.worst_isa, scaled_error(kernel, *first_isa, deal));
            require(scaled_error(kernel, *first_isa, deal) <= mass_tolerance,
                    "V2: AVX2 and scalar three-active masses agree");
          } else {
            first_isa = kernel;
          }
          // A deliberately swapped seat pair must fail. Swapping the vectors turns tie_lower
          // into the reference's tie_higher, so it is detectable exactly where the two differ
          // (not on a board every hand plays, e.g. broadway on board, where both are 0).
          if (mode == ReachMode::Dense) {
            Three swapped;
            pb::three_active_masses(context, view(higher), view(lower), swapped.spans(), scratch);
            const double swap_error = scaled_error(swapped, reference, deal);
            const double asymmetry = scaled_error(reference.tie_lower, reference.tie_higher, deal);
            if (asymmetry > 1e3 * mass_tolerance) {
              stats.weakest_swap_three = std::min(stats.weakest_swap_three, swap_error);
              ++stats.swaps_three;
              require(swap_error > 1e3 * mass_tolerance,
                      "V2: a swapped lower/higher pair fails the three-active comparison (" +
                          board.name + ")");
            } else {
              ++stats.symmetric_three;
            }
          }
        }
        if (hero == 0U) {
          Vector deal_reference{};
          pb::three_seat_deal_mass_reference(context, view(lower), view(higher),
                                             view(deal_reference));
          ++stats.reference_calls;
          require(scaled_error(deal_reference, deal, deal) <= mass_tolerance,
                  "V2: the D3 reference equals the sum of the three-active reference masses");
          for (auto &scratch : scratches) {
            Vector reversed{};
            pb::three_seat_deal_mass(context, view(higher), view(lower), view(reversed), scratch);
            require(scaled_error(reversed, deal_reference, deal_reference) <= mass_tolerance,
                    "V2: D3 is symmetric in its two vectors");
          }
        }
      }
      // Two active: every folder, every hero among the two active seats.
      for (std::uint8_t folder = 0U; folder < 3U; ++folder) {
        for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
          if (hero == folder) {
            continue;
          }
          const auto opponent = static_cast<std::uint8_t>(3U - hero - folder);
          Two reference;
          pb::two_active_masses_reference(context, view(reach[opponent]), view(reach[folder]),
                                          reference.spans());
          ++stats.reference_calls;
          const auto deal = reference.deal();
          std::optional<Two> first_isa;
          for (auto &scratch : scratches) {
            Two kernel;
            pb::two_active_masses(context, view(reach[opponent]), view(reach[folder]),
                                  kernel.spans(), scratch);
            ++stats.kernel_calls;
            const double error = scaled_error(kernel, reference, deal);
            stats.worst_two = std::max(stats.worst_two, error);
            require(error <= mass_tolerance,
                    "V2: two-active masses equal the reference (" + board.name + ", " +
                        mode_name(mode) + ", hero " + std::to_string(hero) + ", folder " +
                        std::to_string(folder) + ", " +
                        pb::multiway_kernel_isa_name(scratch.isa()) + ")");
            if (first_isa) {
              stats.worst_isa = std::max(stats.worst_isa, scaled_error(kernel, *first_isa, deal));
              require(scaled_error(kernel, *first_isa, deal) <= mass_tolerance,
                      "V2: AVX2 and scalar two-active masses agree");
            } else {
              first_isa = kernel;
            }
            // With more than one rank group the opponent's Below set is not empty for some
            // hand, and its win mass weighs the opponent's reach, not the folder's.
            if (mode == ReachMode::Dense) {
              Two swapped;
              pb::two_active_masses(context, view(reach[folder]), view(reach[opponent]),
                                    swapped.spans(), scratch);
              const double swap_error = scaled_error(swapped, reference, deal);
              if (context.distinct_rank_groups() > 1U) {
                stats.weakest_swap_two = std::min(stats.weakest_swap_two, swap_error);
                ++stats.swaps_two;
                require(swap_error > 1e3 * mass_tolerance,
                        "V2: swapping the opponent and the folded seat fails (" + board.name +
                            ")");
              } else {
                ++stats.symmetric_two;
              }
            }
          }
        }
      }
    }
  }
  std::cout << "V2 kernels: " << boards.size() << " boards, " << stats.reference_calls
            << " reference calls, " << stats.kernel_calls << " kernel calls ("
            << sets.size() << " instruction sets), worst scaled error three-active "
            << stats.worst_three << ", two-active " << stats.worst_two << ", D3 "
            << stats.worst_deal << ", AVX2 vs scalar " << stats.worst_isa
            << "; swapped pairs detected three-active " << stats.swaps_three << " (weakest "
            << stats.weakest_swap_three << ", " << stats.symmetric_three
            << " symmetric cases skipped), two-active " << stats.swaps_two << " (weakest "
            << stats.weakest_swap_two << ", " << stats.symmetric_two
            << " single-group cases skipped), tolerance " << mass_tolerance << ", "
            << seconds_since(start) << " s\n";
  require(stats.swaps_three >= 40U && stats.swaps_two >= 80U,
          "V2: the swapped-pair check ran on most dense cases");
}

// A reused scratch must reproduce a fresh one bit for bit (every kernel clears what it wrote),
// and the value kernels equal masses times payoffs.
void test_scratch_and_values(const ca::RankTable &ranks) {
  ca::DeterministicRandom random(0x3A11'0004ULL);
  for (const auto isa : instruction_sets()) {
    pb::MultiwayScratch reused(isa);
    for (int round = 0; round < 6; ++round) {
      const auto context = make_context(ranks, random_board(random));
      const auto mode = static_cast<ReachMode>(round % 3);
      const Vector first = random_reach(random, mode);
      const Vector second = random_reach(random, mode);
      Three reused_masses;
      Two reused_two;
      Vector reused_deal{};
      pb::three_active_masses(context, view(first), view(second), reused_masses.spans(), reused);
      pb::two_active_masses(context, view(first), view(second), reused_two.spans(), reused);
      pb::three_seat_deal_mass(context, view(first), view(second), view(reused_deal), reused);
      pb::MultiwayScratch fresh_three(isa);
      pb::MultiwayScratch fresh_two(isa);
      pb::MultiwayScratch fresh_deal(isa);
      Three fresh_masses;
      Two fresh_two_masses;
      Vector fresh_deal_mass{};
      pb::three_active_masses(context, view(first), view(second), fresh_masses.spans(),
                              fresh_three);
      pb::two_active_masses(context, view(first), view(second), fresh_two_masses.spans(),
                            fresh_two);
      pb::three_seat_deal_mass(context, view(first), view(second), view(fresh_deal_mass),
                               fresh_deal);
      require(same_bits(reused_masses.win, fresh_masses.win) &&
                  same_bits(reused_masses.tie_lower, fresh_masses.tie_lower) &&
                  same_bits(reused_masses.tie_higher, fresh_masses.tie_higher) &&
                  same_bits(reused_masses.tie_both, fresh_masses.tie_both) &&
                  same_bits(reused_masses.lose, fresh_masses.lose) &&
                  same_bits(reused_two.win, fresh_two_masses.win) &&
                  same_bits(reused_two.tie, fresh_two_masses.tie) &&
                  same_bits(reused_two.lose, fresh_two_masses.lose) &&
                  same_bits(reused_deal, fresh_deal_mass),
              "a reused scratch reproduces a fresh one bit for bit");
      const pb::ThreeActivePayoffs three_payoffs{1.5, 0.75, 0.7499, 0.5, -1.25};
      Vector three_values{};
      pb::three_active_values(context, view(first), view(second), three_payoffs,
                              view(three_values), reused);
      const pb::TwoActivePayoffs two_payoffs{2.0, 0.5, -1.0};
      Vector two_values{};
      pb::two_active_values(context, view(first), view(second), two_payoffs, view(two_values),
                            reused);
      for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
        const double expected_three = three_payoffs.win * fresh_masses.win[hand] +
                                      three_payoffs.tie_lower * fresh_masses.tie_lower[hand] +
                                      three_payoffs.tie_higher * fresh_masses.tie_higher[hand] +
                                      three_payoffs.tie_both * fresh_masses.tie_both[hand] +
                                      three_payoffs.lose * fresh_masses.lose[hand];
        const double expected_two = two_payoffs.win * fresh_two_masses.win[hand] +
                                    two_payoffs.tie * fresh_two_masses.tie[hand] +
                                    two_payoffs.lose * fresh_two_masses.lose[hand];
        require(std::bit_cast<std::uint64_t>(three_values[hand]) ==
                        std::bit_cast<std::uint64_t>(expected_three) &&
                    std::bit_cast<std::uint64_t>(two_values[hand]) ==
                        std::bit_cast<std::uint64_t>(expected_two),
                "the value kernels equal masses times payoffs");
      }
    }
  }
  std::cout << "scratch reuse and value kernels: bit-identical\n";
}

// ---------------------------------------------------------------------------------------- V3

void test_v3_uniform_folded_seat(const ca::RankTable &ranks) {
  const auto boards = v2_boards(30);
  ca::DeterministicRandom random(0x3A11'0005ULL);
  Vector ones{};
  ones.fill(1.0);
  double worst = 0.0;
  for (const auto isa : instruction_sets()) {
    pb::MultiwayScratch scratch(isa);
    for (const auto &board : boards) {
      const auto context = make_context(ranks, board.history);
      for (const auto mode : {ReachMode::Dense, ReachMode::Sparse}) {
        const Vector reach = random_reach(random, mode);
        Vector worse{};
        Vector tied{};
        Vector better{};
        Vector fold{};
        pb::showdown_masses(context, view(reach), view(worse), view(tied), view(better));
        pb::fold_mass(context, view(reach), view(fold));
        Two kernel;
        pb::two_active_masses(context, view(reach), view(ones), kernel.spans(), scratch);
        Vector deal{};
        pb::three_seat_deal_mass(context, view(reach), view(ones), view(deal), scratch);
        Vector scaled_deal{};
        Two expected;
        for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
          // C(27, 2) = 351 folded hands avoid the board, h and o.
          expected.win[hand] = 351.0 * worse[hand];
          expected.tie[hand] = 351.0 * tied[hand];
          expected.lose[hand] = 351.0 * better[hand];
          scaled_deal[hand] = 351.0 * fold[hand];
        }
        const double error = std::max(scaled_error(kernel, expected, scaled_deal),
                                      scaled_error(deal, scaled_deal, scaled_deal));
        worst = std::max(worst, error);
        require(error <= mass_tolerance, "V3: with r_f = 1 the two-active masses are 351 x HU (" +
                                             board.name + ", " +
                                             pb::multiway_kernel_isa_name(isa) + ")");
      }
    }
  }
  std::cout << "V3 uniform folded seat: " << boards.size()
            << " boards x dense and sparse opponent reach, worst scaled error " << worst << '\n';
}

// ---------------------------------------------------------------------------------------- V8

pb::CompiledGame compile_monker(const std::string_view name) {
  const auto path = std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "monker" /
                    std::string(name);
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), std::string("fixture parses: ") + std::string(name));
  auto compiled = pb::CompiledGame::compile(parsed.value());
  require(compiled.has_value(), std::string("fixture compiles: ") + std::string(name));
  return std::move(compiled.value());
}

double inner(const Vector &reach, const Vector &values) {
  double total = 0.0;
  for (std::size_t hand = 0; hand < reach.size(); ++hand) {
    total += reach[hand] * values[hand];
  }
  return total;
}

double inner_abs(const Vector &reach, const Vector &values) {
  double total = 0.0;
  for (std::size_t hand = 0; hand < reach.size(); ++hand) {
    total += std::abs(reach[hand] * values[hand]);
  }
  return total;
}

void require_identity(const double left, const double right, const double scale,
                      double &worst, const std::string &message) {
  const double error = std::abs(left - right) / std::max(scale, 1e-300);
  worst = std::max(worst, error);
  require(std::abs(left - right) <= identity_tolerance * scale,
          message + " (relative error " + std::to_string(error) + ")");
}

double payoff(const pb::CompiledGame &game, const std::uint32_t node, const std::uint8_t winners,
              const std::uint8_t seat) {
  return static_cast<double>(game.showdown_payoffs(node, winners)[seat]) * ante_scale;
}

double payoff_sum(const pb::CompiledGame &game, const std::uint32_t node,
                  const std::uint8_t winners) {
  double total = 0.0;
  for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
    total += payoff(game, node, winners, seat);
  }
  return total;
}

std::uint8_t bit(const std::uint8_t seat) { return static_cast<std::uint8_t>(1U << seat); }
std::uint8_t join(const std::uint8_t left, const std::uint8_t right) {
  return static_cast<std::uint8_t>(left | right);
}

void test_v8_identities(const ca::RankTable &ranks, const std::string_view fixture) {
  const auto start = Clock::now();
  const auto game = compile_monker(fixture);
  require(game.config().player_count == 3U, "V8: a 3-seat fixture");
  require(game.config().rake.enabled, "V8: a rake fixture");
  std::vector<std::uint32_t> folds;
  std::vector<std::uint32_t> three_active;
  std::vector<std::uint32_t> two_active;
  for (const auto &node : game.nodes()) {
    if (node.street == gtosd::Street::Preflop) {
      continue;
    }
    if (node.kind == pb::NodeKind::TerminalFold) {
      folds.push_back(node.id);
    } else if (node.kind == pb::NodeKind::TerminalShowdown) {
      const auto active = std::popcount(node.active_mask);
      require(active == 2 || active == 3, "V8: a postflop showdown has 2 or 3 active seats");
      (active == 3 ? three_active : two_active).push_back(node.id);
    }
  }
  require(folds.size() == 1375U && three_active.size() == 472U && two_active.size() == 1854U,
          "V8: 3WAY50 census, 1,375 postflop folds, 472 three-active and 1,854 two-active "
          "showdowns (spec 3.3)");
  ca::DeterministicRandom random(0x3A11'0008ULL ^ std::hash<std::string_view>{}(fixture));
  pb::MultiwayScratch scratch;
  double worst = 0.0;
  std::size_t raked = 0U;
  std::size_t value_calls = 0U;
  for (int board = 0; board < 20; ++board) {
    const auto context = make_context(ranks, random_board(random));
    std::array<Vector, 3> reach{random_reach(random, ReachMode::Dense),
                                random_reach(random, ReachMode::Dense),
                                random_reach(random, ReachMode::Zeros)};
    std::array<Vector, 3> deal{};
    std::array<Three, 3> three{};
    // two[hero][folder]
    std::array<std::array<Two, 3>, 3> two{};
    for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
      const auto seats = other_seats(hero);
      pb::three_seat_deal_mass(context, view(reach[seats[0]]), view(reach[seats[1]]),
                               view(deal[hero]), scratch);
      pb::three_active_masses(context, view(reach[seats[0]]), view(reach[seats[1]]),
                              three[hero].spans(), scratch);
      for (std::uint8_t folder = 0U; folder < 3U; ++folder) {
        if (folder != hero) {
          const auto opponent = static_cast<std::uint8_t>(3U - hero - folder);
          pb::two_active_masses(context, view(reach[opponent]), view(reach[folder]),
                                two[hero][folder].spans(), scratch);
        }
      }
    }
    // <r_s, D3_s> is the total triple mass for every seat.
    const double triples = inner(reach[0], deal[0]);
    for (std::uint8_t seat = 1U; seat < 3U; ++seat) {
      require_identity(inner(reach[seat], deal[seat]), triples,
                       inner_abs(reach[seat], deal[seat]) + triples, worst,
                       "V8: <r_s, D3_s> is equal for the three seats");
    }
    // Winner-set masses of a three-active showdown, read from each seat that wins.
    std::array<double, 8> mass3{};
    const auto tie_with = [&](const std::uint8_t seat, const std::uint8_t other) -> const Vector & {
      return other_seats(seat)[0] == other ? three[seat].tie_lower : three[seat].tie_higher;
    };
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      mass3[bit(seat)] = inner(reach[seat], three[seat].win);
    }
    for (std::uint8_t a = 0U; a < 3U; ++a) {
      for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < 3U; ++b) {
        const double from_a = inner(reach[a], tie_with(a, b));
        const double from_b = inner(reach[b], tie_with(b, a));
        require_identity(from_a, from_b,
                         inner_abs(reach[a], tie_with(a, b)) + inner_abs(reach[b], tie_with(b, a)),
                         worst, "V8: a two-seat tie has the same mass from both winners");
        mass3[join(bit(a), bit(b))] = from_a;
      }
    }
    mass3[7] = inner(reach[0], three[0].tie_both);
    for (std::uint8_t seat = 1U; seat < 3U; ++seat) {
      require_identity(inner(reach[seat], three[seat].tie_both), mass3[7],
                       inner_abs(reach[seat], three[seat].tie_both) + mass3[7], worst,
                       "V8: the three-way tie has the same mass from every seat");
    }
    double all_sets = 0.0;
    for (std::uint8_t winners = 1U; winners < 8U; ++winners) {
      all_sets += mass3[winners];
    }
    require_identity(all_sets, triples, all_sets + triples, worst,
                     "V8: the winner-set masses sum to the triple mass");
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      double without = 0.0;
      for (std::uint8_t winners = 1U; winners < 8U; ++winners) {
        if ((winners & bit(seat)) == 0) {
          without += mass3[winners];
        }
      }
      require_identity(inner(reach[seat], three[seat].lose), without,
                       inner_abs(reach[seat], three[seat].lose) + without, worst,
                       "V8: a seat's losing mass is the mass of the winner sets without it");
    }
    // Sensitivity: a swapped lower/higher pair for seat 0 breaks the cross-seat identity.
    {
      Three swapped;
      pb::three_active_masses(context, view(reach[2]), view(reach[1]), swapped.spans(), scratch);
      const double swapped_01 = inner(reach[0], swapped.tie_lower);
      require(std::abs(swapped_01 - mass3[3]) > 1e3 * identity_tolerance * (mass3[3] + 1.0),
              "V8: a swapped seat pair breaks the {0,1} tie identity");
    }
    // Two-active winner-set masses per folder: mass2[folder][winners].
    std::array<std::array<double, 8>, 3> mass2{};
    for (std::uint8_t folder = 0U; folder < 3U; ++folder) {
      const auto seats = other_seats(folder);
      const auto a = seats[0];
      const auto b = seats[1];
      const auto &by_a = two[a][folder];
      const auto &by_b = two[b][folder];
      require_identity(inner(reach[a], by_a.win), inner(reach[b], by_b.lose),
                       inner_abs(reach[a], by_a.win) + inner_abs(reach[b], by_b.lose), worst,
                       "V8: a's win mass is b's losing mass (folder dead)");
      require_identity(inner(reach[b], by_b.win), inner(reach[a], by_a.lose),
                       inner_abs(reach[b], by_b.win) + inner_abs(reach[a], by_a.lose), worst,
                       "V8: b's win mass is a's losing mass (folder dead)");
      require_identity(inner(reach[a], by_a.tie), inner(reach[b], by_b.tie),
                       inner_abs(reach[a], by_a.tie) + inner_abs(reach[b], by_b.tie), worst,
                       "V8: the tie mass is the same from both active seats");
      mass2[folder][bit(a)] = inner(reach[a], by_a.win);
      mass2[folder][bit(b)] = inner(reach[b], by_b.win);
      mass2[folder][join(bit(a), bit(b))] = inner(reach[a], by_a.tie);
      const double total = mass2[folder][bit(a)] + mass2[folder][bit(b)] +
                           mass2[folder][join(bit(a), bit(b))];
      require_identity(total, triples, total + triples, worst,
                       "V8: the two-active winner-set masses sum to the triple mass");
    }
    // Every postflop terminal: sum_s <r_s, V_s> = sum over winner sets of (sum of payoffs) x mass.
    for (const auto node : folds) {
      const auto payoffs = game.fold_payoffs(node);
      double left = 0.0;
      double scale = 0.0;
      double payoff_total = 0.0;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        const double p = static_cast<double>(payoffs[seat]) * ante_scale;
        left += p * inner(reach[seat], deal[seat]);
        scale += std::abs(p) * inner_abs(reach[seat], deal[seat]);
        payoff_total += p;
      }
      const double right = payoff_total * triples;
      raked += payoff_total < 0.0 ? 1U : 0U;
      require_identity(left, right, scale + std::abs(right), worst,
                       "V8: fold terminal, sum of seat values = -rake x triple mass");
    }
    Vector values{};
    for (const auto node : three_active) {
      double left = 0.0;
      double scale = 0.0;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        const auto seats = other_seats(seat);
        const auto all = static_cast<std::uint8_t>(7U);
        const auto others = static_cast<std::uint8_t>(all & ~bit(seat));
        const double lose = payoff(game, node, others, seat);
        require(lose == payoff(game, node, bit(seats[0]), seat) &&
                    lose == payoff(game, node, bit(seats[1]), seat),
                "V8: a losing seat has one payoff over the winner sets without it");
        const pb::ThreeActivePayoffs p{payoff(game, node, bit(seat), seat),
                                       payoff(game, node, join(bit(seat), bit(seats[0])), seat),
                                       payoff(game, node, join(bit(seat), bit(seats[1])), seat),
                                       payoff(game, node, all, seat), lose};
        pb::three_active_values(context, view(reach[seats[0]]), view(reach[seats[1]]), p,
                                view(values), scratch);
        ++value_calls;
        left += inner(reach[seat], values);
        scale += inner_abs(reach[seat], values);
      }
      double right = 0.0;
      double right_scale = 0.0;
      const double rake = payoff_sum(game, node, 7U);
      for (std::uint8_t winners = 1U; winners < 8U; ++winners) {
        const double sum = payoff_sum(game, node, winners);
        require(std::abs(sum - rake) < 1e-12, "V8: one rake per terminal over the winner sets");
        right += sum * mass3[winners];
        right_scale += std::abs(sum) * mass3[winners];
      }
      raked += rake < 0.0 ? 1U : 0U;
      require_identity(left, right, scale + right_scale, worst,
                       "V8: three-active showdown, sum of seat values = rake through the masses");
    }
    for (const auto node : two_active) {
      const auto active = game.nodes()[node].active_mask;
      const auto folder = static_cast<std::uint8_t>(std::countr_zero(
          static_cast<unsigned>(static_cast<std::uint8_t>(~active) & 7U)));
      const auto seats = other_seats(folder);
      double left = 0.0;
      double scale = 0.0;
      for (const auto seat : seats) {
        const auto opponent = static_cast<std::uint8_t>(3U - seat - folder);
        const pb::TwoActivePayoffs p{payoff(game, node, bit(seat), seat),
                                     payoff(game, node, active, seat),
                                     payoff(game, node, bit(opponent), seat)};
        pb::two_active_values(context, view(reach[opponent]), view(reach[folder]), p,
                              view(values), scratch);
        ++value_calls;
        left += inner(reach[seat], values);
        scale += inner_abs(reach[seat], values);
      }
      // The folded seat: one payoff whoever wins, times its D3 (the two active vectors).
      const double folded = payoff(game, node, active, folder);
      require(folded == payoff(game, node, bit(seats[0]), folder) &&
                  folded == payoff(game, node, bit(seats[1]), folder),
              "V8: the folded seat has one payoff over the winner sets");
      left += folded * inner(reach[folder], deal[folder]);
      scale += std::abs(folded) * inner_abs(reach[folder], deal[folder]);
      double right = 0.0;
      double right_scale = 0.0;
      const double rake = payoff_sum(game, node, active);
      for (const std::uint8_t winners :
           {bit(seats[0]), bit(seats[1]), static_cast<std::uint8_t>(active)}) {
        const double sum = payoff_sum(game, node, winners);
        require(std::abs(sum - rake) < 1e-12, "V8: one rake per terminal over the winner sets");
        right += sum * mass2[folder][winners];
        right_scale += std::abs(sum) * mass2[folder][winners];
      }
      raked += rake < 0.0 ? 1U : 0U;
      require_identity(left, right, scale + right_scale, worst,
                       "V8: two-active showdown, sum of seat values = rake through the masses");
    }
  }
  require(raked > 0U, "V8: some postflop terminals are raked");
  std::cout << "V8 " << fixture << ": 20 boards x (" << folds.size() << " folds, "
            << three_active.size() << " three-active, " << two_active.size()
            << " two-active postflop terminals), " << value_calls << " value-kernel calls, "
            << raked << " raked terminal evaluations, worst relative error " << worst << ", "
            << seconds_since(start) << " s\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      if (name == "--resources-dir") {
        resources_dir = argv[index + 1];
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    const auto start = Clock::now();
    const auto ranks = load_ranks(resources_dir);
    std::cout << "multiway kernels: best instruction set "
              << pb::multiway_kernel_isa_name(pb::best_multiway_kernel_isa()) << ", scratch "
              << pb::MultiwayScratch::bytes() << " bytes\n";
    test_board_context_groups(ranks);
    test_scratch_and_values(ranks);
    test_v3_uniform_folded_seat(ranks);
    test_v2_kernels(ranks);
    for (const std::string_view fixture :
         {"3WAY50_donk_rake.json", "3WAY50_donk_rake25cap2.json", "3WAY50_donk_rake5cap075.json"}) {
      test_v8_identities(ranks, fixture);
    }
    std::cout << "PREFLOP_BLUEPRINT_MULTIWAY_KERNEL_TESTS=PASS assertions=" << assertions << " ("
              << seconds_since(start) << " s)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_MULTIWAY_KERNEL_TESTS=FAIL " << error.what() << " after "
              << assertions << " assertions\n";
    return 1;
  }
}
