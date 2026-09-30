#include "gtosd/card_abstraction/three_way_table.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/showdown.hpp"
#include "resource_file.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <new>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>

namespace gtosd::card_abstraction {
namespace {

constexpr std::string_view resource_kind = "preflop_three_way_classes";
constexpr std::uint32_t class_count = three_way_class_count;
// Accumulator key of one opponent on one board: class * 3 + Versus.
constexpr std::uint32_t key_count = class_count * 3U;
constexpr std::uint32_t live_card_count = 34U;
constexpr std::uint32_t rest_card_count = 29U;
constexpr std::uint64_t hero_board_count = 278'256U;
constexpr std::uint32_t tasks_per_hero = 32U;
constexpr unsigned maximum_threads = 256U;
constexpr std::uint8_t no_position = 0xFFU;
// C(27,2): folded hands left by one heads-up runout; C(32,2): folded hands
// disjoint from a heads-up pair.
constexpr std::uint64_t folded_hands_per_runout = 351U;
constexpr std::uint64_t folded_hands_per_pair = 496U;

using Clock = std::chrono::steady_clock;
// Upper triangle (row key <= column key) of the ordered-pair counts D[k1][k2],
// packed row by row (118 KB instead of 236 KB, it stays in L2), modulo 2^32:
// the per-board terms subtract, only the final sums are counts.
using Accumulator = std::vector<std::uint32_t>;
constexpr std::size_t triangle_size = key_count * (key_count + 1U) / 2U;

// Slot of (low, high), low <= high, is triangle_base[low] + high.
constexpr std::array<std::uint32_t, key_count> make_triangle_base() noexcept {
  std::array<std::uint32_t, key_count> base{};
  std::uint32_t start = 0U;
  for (std::uint32_t low = 0U; low < key_count; ++low) {
    base[low] = start - low;
    start += key_count - low;
  }
  return base;
}

constexpr auto triangle_base = make_triangle_base();

struct ClassMembers {
  std::array<std::array<std::uint16_t, 12>, class_count> ids{};
  std::array<std::uint8_t, class_count> size{};
};

ClassMembers make_class_members() {
  ClassMembers members;
  const auto &table = combo_table();
  for (std::uint16_t combo = 0U; combo < combo_count; ++combo) {
    const auto hand_class = table.hand_class[combo];
    members.ids[hand_class][members.size[hand_class]++] = combo;
  }
  return members;
}

const ClassMembers &class_members() {
  static const ClassMembers members = make_class_members();
  return members;
}

// Class of the combo {p, q} in both orders.
std::array<std::array<std::uint8_t, deck_cards>, deck_cards> make_pair_classes() {
  std::array<std::array<std::uint8_t, deck_cards>, deck_cards> classes{};
  const auto &table = combo_table();
  for (std::size_t combo = 0U; combo < combo_count; ++combo) {
    const auto [low, high] = table.cards[combo];
    classes[low][high] = table.hand_class[combo];
    classes[high][low] = table.hand_class[combo];
  }
  return classes;
}

const std::array<std::array<std::uint8_t, deck_cards>, deck_cards> &pair_classes() {
  static const auto classes = make_pair_classes();
  return classes;
}

std::uint32_t class_size(const std::uint8_t hand_class) noexcept {
  return class_mass(static_cast<HandClassId>(hand_class));
}

struct HeroContext {
  std::uint16_t combo{0U};
  std::array<std::uint8_t, 2> cards{};
  // Cards outside the hero combo, ascending; boards are 5-subsets of positions.
  std::array<std::uint8_t, live_card_count> live{};
  std::array<std::uint8_t, deck_cards> live_position{};
  // Suit permutations other than the identity that map the hero combo onto
  // itself, as card maps.
  std::vector<std::array<std::uint8_t, deck_cards>> symmetries;
};

HeroContext make_hero_context(const std::uint16_t hero_combo, const bool board_symmetry) {
  HeroContext hero;
  hero.combo = hero_combo;
  hero.cards = combo_table().cards[hero_combo];
  std::uint8_t position = 0U;
  for (std::uint8_t card = 0U; card < deck_cards; ++card) {
    if (card == hero.cards[0] || card == hero.cards[1]) {
      hero.live_position[card] = no_position;
      continue;
    }
    hero.live_position[card] = position;
    hero.live[position++] = card;
  }
  if (!board_symmetry) {
    return hero;
  }
  std::array<std::uint8_t, 4> suits{0U, 1U, 2U, 3U};
  while (std::next_permutation(suits.begin(), suits.end())) {
    std::array<std::uint8_t, deck_cards> map{};
    for (std::uint8_t card = 0U; card < deck_cards; ++card) {
      map[card] = static_cast<std::uint8_t>((card / 4U) * 4U + suits[card % 4U]);
    }
    const auto first = map[hero.cards[0]];
    const auto second = map[hero.cards[1]];
    if ((first == hero.cards[0] && second == hero.cards[1]) ||
        (first == hero.cards[1] && second == hero.cards[0])) {
      hero.symmetries.push_back(map);
    }
  }
  return hero;
}

std::uint64_t colex_positions(const std::array<std::uint8_t, 5> &positions) noexcept {
  return binomial(positions[0], 1U) + binomial(positions[1], 2U) + binomial(positions[2], 3U) +
         binomial(positions[3], 4U) + binomial(positions[4], 5U);
}

// Colex successor of a strictly increasing 5-subset of {0..n-1}.
void next_colex(std::array<std::uint8_t, 5> &positions) noexcept {
  for (std::size_t slot = 0U; slot < positions.size(); ++slot) {
    const auto limit = slot + 1U < positions.size() ? positions[slot + 1U]
                                                    : static_cast<std::uint8_t>(live_card_count);
    if (positions[slot] + 1U < limit) {
      ++positions[slot];
      for (std::size_t lower = 0U; lower < slot; ++lower) {
        positions[lower] = static_cast<std::uint8_t>(lower);
      }
      return;
    }
  }
}

// Weight of the board when it is the lowest colex index of its orbit under the
// hero's symmetries (the orbit size), zero otherwise.
std::uint32_t orbit_weight(const HeroContext &hero, const std::array<std::uint8_t, 5> &positions,
                           const std::uint64_t index) noexcept {
  std::array<std::uint64_t, 6> images{};
  std::uint32_t distinct = 0U;
  for (const auto &map : hero.symmetries) {
    std::array<std::uint8_t, 5> image{};
    for (std::size_t slot = 0U; slot < image.size(); ++slot) {
      image[slot] = hero.live_position[map[hero.live[positions[slot]]]];
    }
    std::sort(image.begin(), image.end());
    const auto image_index = colex_positions(image);
    if (image_index < index) {
      return 0U;
    }
    if (image_index != index && std::find(images.begin(), images.begin() + distinct, image_index) ==
                                    images.begin() + distinct) {
      images[distinct++] = image_index;
    }
  }
  return 1U + distinct;
}

// Adds one board of one hero to the accumulator, weighted. With n[k] the live
// opponents of key k and the ordered disjoint pairs D[k1][k2]:
//   D = n n^T - diag(n) - (ordered pairs of distinct combos sharing a card),
// and two distinct combos share at most one card, so the last term is a sum
// over the shared card c of m_c m_c^T - diag(m_c), m_c the key histogram of
// the 28 combos holding c. Both products run over the present keys only (84
// of the 243 on average for the AKo hero, at most 28 per card), in ascending
// key order.
class BoardKernel {
public:
  BoardKernel(const RankTable &ranks, const HeroContext &hero) : ranks_(ranks), hero_(hero) {}

  void add(const std::array<std::uint8_t, 5> &positions, const std::uint32_t weight,
           std::uint32_t *accumulator) {
    std::array<std::uint8_t, 5> board{};
    std::uint64_t board_mask = 0U;
    for (std::size_t slot = 0U; slot < board.size(); ++slot) {
      board[slot] = hero_.live[positions[slot]];
      board_mask |= std::uint64_t{1} << board[slot];
    }
    // Colex index of hand {p < q} plus board, from the count of board cards
    // below each hand card: board card k sits at position k, k + 1 or k + 2.
    std::array<std::array<std::uint64_t, 6>, 3> prefix{};
    for (std::size_t shift = 0U; shift < 3U; ++shift) {
      for (std::uint32_t slot = 0U; slot < 5U; ++slot) {
        prefix[shift][slot + 1U] =
            prefix[shift][slot] +
            binomial(board[slot], slot + static_cast<std::uint32_t>(shift) + 1U);
      }
    }
    std::array<std::uint8_t, deck_cards> below{};
    std::uint8_t seen = 0U;
    std::uint32_t rest_count = 0U;
    for (std::uint8_t card = 0U; card < deck_cards; ++card) {
      below[card] = seen;
      if (((board_mask >> card) & 1U) != 0U) {
        ++seen;
      } else if (card != hero_.cards[0] && card != hero_.cards[1]) {
        rest_[rest_count++] = card;
      }
    }
    const auto lower_term = [&](const std::uint8_t card) {
      const auto count = below[card];
      return binomial(card, count + 1U) + prefix[0][count] - prefix[1][count];
    };
    const auto upper_term = [&](const std::uint8_t card) {
      const auto count = below[card];
      return binomial(card, count + 2U) + prefix[1][count] - prefix[2][count] + prefix[2][5];
    };
    const auto hero_rank = ranks_.rank_at(lower_term(hero_.cards[0]) + upper_term(hero_.cards[1]));
    for (std::uint32_t index = 0U; index < rest_card_count; ++index) {
      lower_[index] = lower_term(rest_[index]);
      upper_[index] = upper_term(rest_[index]);
    }

    const auto &classes = pair_classes();
    for (std::uint32_t i = 0U; i + 1U < rest_card_count; ++i) {
      const auto low = lower_[i];
      const auto &class_row = classes[rest_[i]];
      for (std::uint32_t j = i + 1U; j < rest_card_count; ++j) {
        const auto rank = ranks_.rank_at(low + upper_[j]);
        const auto versus = static_cast<std::uint32_t>(rank >= hero_rank) +
                            static_cast<std::uint32_t>(rank > hero_rank);
        const auto key = static_cast<std::uint16_t>(3U * class_row[rest_[j]] + versus);
        keys_[i][j] = key;
        keys_[j][i] = key;
        ++counts_[key];
      }
    }

    std::uint32_t present = 0U;
    for (std::uint16_t key = 0U; key < key_count; ++key) {
      if (counts_[key] != 0U) {
        present_[present++] = key;
      }
    }
    add_outer_product(present, weight, accumulator);

    // Per shared card: the key histogram of its holders, kept sorted.
    for (std::uint32_t shared = 0U; shared < rest_card_count; ++shared) {
      present = 0U;
      for (std::uint32_t other = 0U; other < rest_card_count; ++other) {
        if (other == shared) {
          continue;
        }
        const auto key = keys_[shared][other];
        if (counts_[key]++ != 0U) {
          continue;
        }
        auto slot = present++;
        for (; slot > 0U && present_[slot - 1U] > key; --slot) {
          present_[slot] = present_[slot - 1U];
        }
        present_[slot] = key;
      }
      add_outer_product(present, 0U - weight, accumulator);
    }
  }

private:
  // accumulator += weight * (c c^T - diag(c)) over the upper triangle, for the
  // histogram c = counts_ on the sorted keys present_[0, present); clears c.
  void add_outer_product(const std::uint32_t present, const std::uint32_t weight,
                         std::uint32_t *accumulator) {
    for (std::uint32_t a = 0U; a < present; ++a) {
      const auto key_a = present_[a];
      const std::uint32_t count_a = counts_[key_a];
      auto *row = accumulator + triangle_base[key_a];
      row[key_a] += weight * (count_a * count_a - count_a);
      const auto scaled = weight * count_a;
      for (std::uint32_t b = a + 1U; b < present; ++b) {
        const auto key_b = present_[b];
        row[key_b] += scaled * counts_[key_b];
      }
    }
    for (std::uint32_t a = 0U; a < present; ++a) {
      counts_[present_[a]] = 0U;
    }
  }

  const RankTable &ranks_;
  const HeroContext &hero_;
  std::array<std::uint8_t, rest_card_count> rest_{};
  std::array<std::uint64_t, rest_card_count> lower_{};
  std::array<std::uint64_t, rest_card_count> upper_{};
  std::array<std::array<std::uint16_t, rest_card_count>, rest_card_count> keys_{};
  std::array<std::uint16_t, key_count> counts_{};
  std::array<std::uint16_t, key_count> present_{};
};

struct HeroCost {
  double task_seconds{0.0};
  std::uint64_t boards{0U};
};

// Accumulates every hero combo over all its boards, tasks = (hero, board range)
// pulled by the workers in order. Integer sums: the result does not depend on
// the thread count or the chunking.
std::vector<Accumulator> accumulate_heroes(const RankTable &ranks,
                                           const std::vector<HeroContext> &heroes,
                                           const unsigned threads, std::vector<HeroCost> &costs) {
  std::vector<Accumulator> totals(heroes.size(), Accumulator(triangle_size, 0U));
  costs.assign(heroes.size(), HeroCost{});
  std::vector<std::mutex> locks(heroes.size());
  const auto task_count = heroes.size() * tasks_per_hero;
  std::atomic<std::size_t> next_task{0U};
  const auto work = [&] {
    Accumulator local(triangle_size, 0U);
    for (auto task = next_task.fetch_add(1U); task < task_count; task = next_task.fetch_add(1U)) {
      const auto started = Clock::now();
      const auto hero_index = task / tasks_per_hero;
      const auto chunk = task % tasks_per_hero;
      const auto &hero = heroes[hero_index];
      const auto first = hero_board_count * chunk / tasks_per_hero;
      const auto last = hero_board_count * (chunk + 1U) / tasks_per_hero;
      std::fill(local.begin(), local.end(), 0U);
      BoardKernel kernel(ranks, hero);
      std::array<std::uint8_t, 5> positions{};
      static_cast<void>(subset_from_index(first, positions));
      std::uint64_t boards = 0U;
      for (auto index = first; index < last; ++index, next_colex(positions)) {
        const auto weight = hero.symmetries.empty() ? 1U : orbit_weight(hero, positions, index);
        if (weight == 0U) {
          continue;
        }
        kernel.add(positions, weight, local.data());
        ++boards;
      }
      const auto seconds = std::chrono::duration<double>(Clock::now() - started).count();
      const std::lock_guard<std::mutex> guard(locks[hero_index]);
      auto &total = totals[hero_index];
      for (std::size_t slot = 0U; slot < total.size(); ++slot) {
        total[slot] += local[slot];
      }
      costs[hero_index].task_seconds += seconds;
      costs[hero_index].boards += boards;
    }
  };
  std::vector<std::thread> workers;
  workers.reserve(threads);
  for (unsigned worker = 0U; worker < threads; ++worker) {
    workers.emplace_back(work);
  }
  for (auto &worker : workers) {
    worker.join();
  }
  return totals;
}

// Expands one hero's accumulator into its 81 x 81 entries, A-major.
void write_rows(const Accumulator &total, const std::uint16_t hero_combo,
                std::span<ThreeWayEntry> rows) {
  for (std::uint8_t first = 0U; first < class_count; ++first) {
    for (std::uint8_t second = 0U; second < class_count; ++second) {
      auto &entry = rows[static_cast<std::size_t>(first) * class_count + second];
      entry.pairs = three_way_pair_count(hero_combo, first, second);
      for (std::uint32_t x = 0U; x < 3U; ++x) {
        for (std::uint32_t y = 0U; y < 3U; ++y) {
          const auto key_first = 3U * first + x;
          const auto key_second = 3U * second + y;
          const auto low = std::min(key_first, key_second);
          const auto high = std::max(key_first, key_second);
          entry.cells[3U * x + y] = total[triangle_base[low] + high];
        }
      }
    }
  }
}

bool valid_threads(const unsigned threads) noexcept {
  return threads != 0U && threads <= maximum_threads;
}

bool combos_disjoint(const std::uint16_t a, const std::uint16_t b) noexcept {
  const auto &masks = combo_table().masks;
  return (masks[a] & masks[b]) == 0U;
}

Versus versus_of(const std::uint16_t hero_rank, const std::uint16_t opponent_rank) noexcept {
  if (hero_rank > opponent_rank) {
    return Versus::Better;
  }
  return hero_rank == opponent_rank ? Versus::Tie : Versus::Worse;
}

std::uint64_t pot_sixths(const ThreeWayEntry &entry) noexcept {
  return 6U * static_cast<std::uint64_t>(entry.cell(Versus::Better, Versus::Better)) +
         3U * (static_cast<std::uint64_t>(entry.cell(Versus::Tie, Versus::Better)) +
               entry.cell(Versus::Better, Versus::Tie)) +
         2U * static_cast<std::uint64_t>(entry.cell(Versus::Tie, Versus::Tie));
}

} // namespace

std::uint16_t ThreeWayTable::representative_of(const std::uint8_t hand_class) noexcept {
  return class_members().ids[hand_class][0];
}

std::uint32_t three_way_pair_count(const std::uint16_t hero_combo, const std::uint8_t first,
                                   const std::uint8_t second) noexcept {
  const auto &members = class_members();
  const auto &masks = combo_table().masks;
  const auto hero_mask = masks[hero_combo];
  std::uint32_t pairs = 0U;
  for (std::uint8_t a = 0U; a < members.size[first]; ++a) {
    const auto mask_a = masks[members.ids[first][a]];
    if ((mask_a & hero_mask) != 0U) {
      continue;
    }
    for (std::uint8_t b = 0U; b < members.size[second]; ++b) {
      const auto mask_b = masks[members.ids[second][b]];
      pairs += static_cast<std::uint32_t>((mask_b & (hero_mask | mask_a)) == 0U);
    }
  }
  return pairs;
}

Result<std::vector<ThreeWayEntry>, ResourceError>
ThreeWayTable::build_hero_rows(const RankTable &ranks, const std::uint16_t hero_combo,
                               const unsigned threads, const bool board_symmetry) {
  using Built = Result<std::vector<ThreeWayEntry>, ResourceError>;
  if (!valid_threads(threads) || hero_combo >= combo_count) {
    return Built::failure(ResourceError::InvalidInput);
  }
  try {
    const std::vector<HeroContext> heroes{make_hero_context(hero_combo, board_symmetry)};
    std::vector<HeroCost> costs;
    const auto totals = accumulate_heroes(ranks, heroes, threads, costs);
    std::vector<ThreeWayEntry> rows(static_cast<std::size_t>(class_count) * class_count);
    write_rows(totals[0], hero_combo, rows);
    return Built::success(std::move(rows));
  } catch (const std::bad_alloc &) {
    return Built::failure(ResourceError::MemoryFailure);
  } catch (const std::system_error &) {
    return Built::failure(ResourceError::InvalidInput);
  }
}

Result<ThreeWayTable, ResourceError> ThreeWayTable::build(const RankTable &ranks,
                                                          const ThreeWayBuildOptions &options,
                                                          ThreeWayBuildTiming *timing) {
  using Built = Result<ThreeWayTable, ResourceError>;
  if (!valid_threads(options.threads)) {
    return Built::failure(ResourceError::InvalidInput);
  }
  std::vector<std::uint8_t> hero_classes = options.hero_classes;
  if (hero_classes.empty()) {
    for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
      hero_classes.push_back(hand_class);
    }
  }
  std::sort(hero_classes.begin(), hero_classes.end());
  if (hero_classes.back() >= class_count ||
      std::adjacent_find(hero_classes.begin(), hero_classes.end()) != hero_classes.end()) {
    return Built::failure(ResourceError::InvalidInput);
  }
  try {
    const auto started = Clock::now();
    std::vector<HeroContext> heroes;
    heroes.reserve(hero_classes.size());
    for (const auto hand_class : hero_classes) {
      heroes.push_back(make_hero_context(representative_of(hand_class), options.board_symmetry));
    }
    std::vector<HeroCost> costs;
    const auto totals = accumulate_heroes(ranks, heroes, options.threads, costs);
    ThreeWayTable table;
    table.representatives_.fill(three_way_unbuilt_hero);
    table.entries_.assign(three_way_entry_count, ThreeWayEntry{});
    for (std::size_t index = 0U; index < hero_classes.size(); ++index) {
      const auto hand_class = hero_classes[index];
      table.representatives_[hand_class] = heroes[index].combo;
      write_rows(totals[index], heroes[index].combo,
                 std::span<ThreeWayEntry>(table.entries_)
                     .subspan(entry_index(hand_class, 0U, 0U),
                              static_cast<std::size_t>(class_count) * class_count));
      if (timing != nullptr) {
        timing->hero_task_seconds[hand_class] = costs[index].task_seconds;
        timing->hero_boards[hand_class] = costs[index].boards;
      }
    }
    table.finalize(ranks.fingerprint());
    if (timing != nullptr) {
      timing->wall_seconds = std::chrono::duration<double>(Clock::now() - started).count();
    }
    return Built::success(std::move(table));
  } catch (const std::bad_alloc &) {
    return Built::failure(ResourceError::MemoryFailure);
  } catch (const std::system_error &) {
    return Built::failure(ResourceError::InvalidInput);
  }
}

bool ThreeWayTable::complete() const noexcept {
  return std::none_of(representatives_.begin(), representatives_.end(),
                      [](const std::uint16_t combo) { return combo == three_way_unbuilt_hero; });
}

ThreeWayMass ThreeWayTable::three_way_mass(const std::uint8_t hero, const std::uint8_t first,
                                           const std::uint8_t second) const noexcept {
  const auto &value = entry(hero, first, second);
  constexpr auto runouts = static_cast<double>(three_way_runout_count);
  const auto alone = value.cell(Versus::Better, Versus::Better);
  const auto with_first = value.cell(Versus::Tie, Versus::Better);
  const auto with_second = value.cell(Versus::Better, Versus::Tie);
  const auto all_three = value.cell(Versus::Tie, Versus::Tie);
  const auto losing = static_cast<std::uint64_t>(value.pairs) * three_way_runout_count -
                      (static_cast<std::uint64_t>(alone) + with_first + with_second + all_three);
  return ThreeWayMass{alone / runouts, with_first / runouts, with_second / runouts,
                      all_three / runouts, static_cast<double>(losing) / runouts};
}

TwoWayMass ThreeWayTable::two_way_mass(const std::uint8_t hero, const std::uint8_t opponent,
                                       const std::uint8_t folded) const noexcept {
  const auto &value = entry(hero, opponent, folded);
  constexpr auto runouts = static_cast<double>(three_way_runout_count);
  return TwoWayMass{static_cast<double>(value.against_first(Versus::Better)) / runouts,
                    static_cast<double>(value.against_first(Versus::Tie)) / runouts,
                    static_cast<double>(value.against_first(Versus::Worse)) / runouts};
}

TwoWayMass two_way_mass_folded_ignored(const AllInTable &heads_up, const std::uint8_t hero,
                                       const std::uint8_t opponent,
                                       const std::uint8_t folded) noexcept {
  const auto &members = class_members();
  const auto &masks = combo_table().masks;
  const auto hero_combo = ThreeWayTable::representative_of(hero);
  const auto hero_mask = masks[hero_combo];
  constexpr auto runouts = static_cast<double>(all_in_runout_count);
  TwoWayMass mass;
  for (std::uint8_t o = 0U; o < members.size[opponent]; ++o) {
    const auto opponent_combo = members.ids[opponent][o];
    const auto opponent_mask = masks[opponent_combo];
    if ((opponent_mask & hero_mask) != 0U) {
      continue;
    }
    std::uint32_t weight = 0U;
    for (std::uint8_t f = 0U; f < members.size[folded]; ++f) {
      const auto folded_mask = masks[members.ids[folded][f]];
      weight += static_cast<std::uint32_t>((folded_mask & (hero_mask | opponent_mask)) == 0U);
    }
    if (weight == 0U) {
      continue;
    }
    const auto outcome = heads_up.outcome(hero_combo, opponent_combo);
    mass.wins += weight * (outcome.wins / runouts);
    mass.ties += weight * (outcome.ties / runouts);
    mass.losses += weight * (outcome.losses / runouts);
  }
  return mass;
}

TwoWayMass two_way_mass(const ThreeWayTable &table, const AllInTable &heads_up,
                        const FoldedCards folded_cards, const std::uint8_t hero,
                        const std::uint8_t opponent, const std::uint8_t folded) noexcept {
  return folded_cards == FoldedCards::Dead
             ? table.two_way_mass(hero, opponent, folded)
             : two_way_mass_folded_ignored(heads_up, hero, opponent, folded);
}

Result<ComboTripleCounts, ResourceError> count_combo_triple(const RankTable &ranks,
                                                            const std::uint16_t hero,
                                                            const std::uint16_t first,
                                                            const std::uint16_t second) {
  using Counted = Result<ComboTripleCounts, ResourceError>;
  if (hero >= combo_count || first >= combo_count || second >= combo_count ||
      !combos_disjoint(hero, first) || !combos_disjoint(hero, second) ||
      !combos_disjoint(first, second)) {
    return Counted::failure(ResourceError::InvalidInput);
  }
  const auto &table = combo_table();
  const std::array<std::array<std::uint8_t, 2>, 3> hands{table.cards[hero], table.cards[first],
                                                         table.cards[second]};
  const auto dealt = table.masks[hero] | table.masks[first] | table.masks[second];
  std::vector<std::uint8_t> rest;
  for (std::uint8_t card = 0U; card < deck_cards; ++card) {
    if (((dealt >> card) & 1U) == 0U) {
      rest.push_back(card);
    }
  }
  ComboTripleCounts counts;
  std::array<std::uint8_t, 5> positions{};
  std::array<std::uint8_t, 5> board{};
  const auto runouts = binomial(static_cast<std::uint32_t>(rest.size()), 5U);
  for (std::uint64_t index = 0U; index < runouts; ++index) {
    static_cast<void>(subset_from_index(index, positions));
    for (std::size_t slot = 0U; slot < board.size(); ++slot) {
      board[slot] = rest[positions[slot]];
    }
    std::array<std::uint16_t, 3> hand_ranks{};
    for (std::size_t seat = 0U; seat < hands.size(); ++seat) {
      hand_ranks[seat] = ranks.rank_of(hands[seat], board);
    }
    const auto best = std::max({hand_ranks[0], hand_ranks[1], hand_ranks[2]});
    std::uint32_t mask = 0U;
    for (std::uint32_t seat = 0U; seat < 3U; ++seat) {
      mask |= static_cast<std::uint32_t>(hand_ranks[seat] == best) << seat;
    }
    ++counts.by_winners[mask - 1U];
    const auto x = static_cast<std::uint32_t>(versus_of(hand_ranks[0], hand_ranks[1]));
    const auto y = static_cast<std::uint32_t>(versus_of(hand_ranks[0], hand_ranks[2]));
    ++counts.cells[3U * x + y];
  }
  return Counted::success(counts);
}

Result<ThreeWayEntry, ResourceError> evaluate_entry_by_showdown(const std::uint16_t hero_combo,
                                                                const std::uint8_t first,
                                                                const std::uint8_t second,
                                                                const unsigned threads) {
  using Evaluated = Result<ThreeWayEntry, ResourceError>;
  if (!valid_threads(threads) || hero_combo >= combo_count || first >= class_count ||
      second >= class_count) {
    return Evaluated::failure(ResourceError::InvalidInput);
  }
  const auto &members = class_members();
  std::vector<std::array<std::uint16_t, 2>> pairs;
  for (std::uint8_t a = 0U; a < members.size[first]; ++a) {
    for (std::uint8_t b = 0U; b < members.size[second]; ++b) {
      const auto combo_a = members.ids[first][a];
      const auto combo_b = members.ids[second][b];
      if (combos_disjoint(hero_combo, combo_a) && combos_disjoint(hero_combo, combo_b) &&
          combos_disjoint(combo_a, combo_b)) {
        pairs.push_back({combo_a, combo_b});
      }
    }
  }
  const auto combos = all_combos();
  std::vector<std::array<std::uint32_t, 9>> partial(threads, std::array<std::uint32_t, 9>{});
  std::atomic<bool> failed{false};
  const auto work = [&](const unsigned worker) {
    auto &cells = partial[worker];
    std::vector<CardId> board(5U);
    std::vector<std::array<CardId, 2>> hands(3U);
    for (std::size_t pair = worker; pair < pairs.size(); pair += threads) {
      const std::array<std::uint16_t, 3> seats{hero_combo, pairs[pair][0], pairs[pair][1]};
      std::uint64_t dealt = 0U;
      for (std::size_t seat = 0U; seat < seats.size(); ++seat) {
        const auto &combo = combos[seats[seat]];
        hands[seat] = {combo.first, combo.second};
        dealt |= combo.first.mask() | combo.second.mask();
      }
      std::vector<CardId> rest;
      for (std::uint8_t card = 0U; card < deck_cards; ++card) {
        if (((dealt >> card) & 1U) == 0U) {
          rest.push_back(CardId::from_index(card).value());
        }
      }
      const auto count = rest.size();
      for (std::size_t c0 = 0U; c0 < count; ++c0) {
        for (std::size_t c1 = c0 + 1U; c1 < count; ++c1) {
          for (std::size_t c2 = c1 + 1U; c2 < count; ++c2) {
            for (std::size_t c3 = c2 + 1U; c3 < count; ++c3) {
              for (std::size_t c4 = c3 + 1U; c4 < count; ++c4) {
                board[0] = rest[c0];
                board[1] = rest[c1];
                board[2] = rest[c2];
                board[3] = rest[c3];
                board[4] = rest[c4];
                const auto showdown = evaluate_showdown(hands, board);
                if (!showdown) {
                  failed = true;
                  return;
                }
                const auto &values = showdown.value().values;
                const auto x = values[0] > values[1] ? 0U : (values[0] == values[1] ? 1U : 2U);
                const auto y = values[0] > values[2] ? 0U : (values[0] == values[2] ? 1U : 2U);
                ++cells[3U * x + y];
              }
            }
          }
        }
      }
    }
  };
  try {
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned worker = 0U; worker < threads; ++worker) {
      workers.emplace_back(work, worker);
    }
    for (auto &worker : workers) {
      worker.join();
    }
  } catch (const std::bad_alloc &) {
    return Evaluated::failure(ResourceError::MemoryFailure);
  } catch (const std::system_error &) {
    return Evaluated::failure(ResourceError::InvalidInput);
  }
  if (failed) {
    return Evaluated::failure(ResourceError::IntegrityFailure);
  }
  ThreeWayEntry entry;
  entry.pairs = static_cast<std::uint32_t>(pairs.size());
  for (const auto &cells : partial) {
    for (std::size_t cell = 0U; cell < cells.size(); ++cell) {
      entry.cells[cell] += cells[cell];
    }
  }
  return Evaluated::success(entry);
}

ThreeWayIdentityReport check_three_way_identities(const ThreeWayTable &table,
                                                  const AllInTable *heads_up) {
  ThreeWayIdentityReport report;
  const auto check = [&report](const std::size_t identity, const bool holds) {
    ++report.checks[identity];
    report.failures[identity] += static_cast<std::uint64_t>(!holds);
  };
  constexpr std::uint64_t runouts = three_way_runout_count;
  for (std::uint8_t h = 0U; h < class_count; ++h) {
    if (!table.hero_built(h)) {
      continue;
    }
    const std::uint64_t size_h = class_size(h);
    for (std::uint8_t a = 0U; a < class_count; ++a) {
      const std::uint64_t size_a = class_size(a);
      for (std::uint8_t b = 0U; b < class_count; ++b) {
        const std::uint64_t size_b = class_size(b);
        const auto &value = table.entry(h, a, b);
        const auto &swapped = table.entry(h, b, a);
        // V1: the 9 cells cover every runout of every pair.
        check(0U, value.total() == value.pairs * runouts);
        // V2: pair counts, symmetric in the opponents and the same number of
        // disjoint combo triples from every seat.
        check(1U, value.pairs == swapped.pairs);
        const auto triples = size_h * value.pairs;
        if (table.hero_built(a)) {
          check(1U, triples == size_a * table.entry(a, h, b).pairs);
        }
        if (table.hero_built(b)) {
          check(1U, triples == size_b * table.entry(b, a, h).pairs);
        }
        // V3: swapping the opponents transposes the cells.
        bool transposed = true;
        for (std::uint32_t x = 0U; x < 3U; ++x) {
          for (std::uint32_t y = 0U; y < 3U; ++y) {
            transposed = transposed && value.cells[3U * x + y] == swapped.cells[3U * y + x];
          }
        }
        check(2U, transposed);
        // V4: hero beats first = first loses to hero, over all combos.
        if (table.hero_built(a)) {
          const auto &mirrored = table.entry(a, h, b);
          check(3U, size_h * value.against_first(Versus::Better) ==
                        size_a * mirrored.against_first(Versus::Worse));
          check(3U, size_h * value.against_first(Versus::Tie) ==
                        size_a * mirrored.against_first(Versus::Tie));
        }
        // V5: the three pot shares add up to the pot (in sixths).
        if (table.hero_built(a) && table.hero_built(b)) {
          const auto shares = size_h * pot_sixths(value) +
                              size_a * pot_sixths(table.entry(a, h, b)) +
                              size_b * pot_sixths(table.entry(b, h, a));
          check(4U, shares == 6U * runouts * triples);
        }
      }
    }
  }
  if (heads_up == nullptr) {
    return report;
  }
  // V6: summed over the folded class, the 2-way rows are C(27,2) times the
  // heads-up counts of the same pair.
  const auto &members = class_members();
  const auto &masks = combo_table().masks;
  for (std::uint8_t h = 0U; h < class_count; ++h) {
    if (!table.hero_built(h)) {
      continue;
    }
    const auto hero_combo = table.representatives()[h];
    for (std::uint8_t a = 0U; a < class_count; ++a) {
      std::array<std::uint64_t, 3> rows{};
      std::uint64_t pairs = 0U;
      for (std::uint8_t f = 0U; f < class_count; ++f) {
        const auto &value = table.entry(h, a, f);
        rows[0] += value.against_first(Versus::Better);
        rows[1] += value.against_first(Versus::Tie);
        rows[2] += value.against_first(Versus::Worse);
        pairs += value.pairs;
        if (value.pairs == 0U) {
          continue;
        }
        const auto dead = table.two_way_mass(h, a, f);
        const auto ignored = two_way_mass_folded_ignored(*heads_up, h, a, f);
        const auto shift = std::abs((dead.wins + 0.5 * dead.ties) / value.pairs -
                                    (ignored.wins + 0.5 * ignored.ties) / value.pairs);
        if (shift > report.max_folded_equity_shift) {
          report.max_folded_equity_shift = shift;
          report.max_folded_shift_classes = {h, a, f};
        }
      }
      std::array<std::uint64_t, 3> heads_up_rows{};
      std::uint64_t opponents = 0U;
      for (std::uint8_t index = 0U; index < members.size[a]; ++index) {
        const auto opponent = members.ids[a][index];
        if ((masks[opponent] & masks[hero_combo]) != 0U) {
          continue;
        }
        const auto outcome = heads_up->outcome(hero_combo, opponent);
        heads_up_rows[0] += outcome.wins;
        heads_up_rows[1] += outcome.ties;
        heads_up_rows[2] += outcome.losses;
        ++opponents;
      }
      for (std::size_t row = 0U; row < rows.size(); ++row) {
        check(5U, rows[row] == folded_hands_per_runout * heads_up_rows[row]);
      }
      check(5U, pairs == folded_hands_per_pair * opponents);
    }
  }
  return report;
}

std::vector<std::uint8_t> ThreeWayTable::payload() const {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(static_cast<std::size_t>(payload_bytes()));
  detail::append_little(bytes, static_cast<std::uint64_t>(entries_.size()));
  for (const auto combo : representatives_) {
    detail::append_little(bytes, combo);
  }
  for (const auto &entry : entries_) {
    detail::append_little(bytes, entry.pairs);
    for (const auto cell : entry.cells) {
      detail::append_little(bytes, cell);
    }
  }
  return bytes;
}

void ThreeWayTable::finalize(const std::string &rank_fingerprint) {
  auto hash = detail::fnv1a_text("gtosd.card_abstraction.preflop_three_way_classes.v1|");
  hash = detail::fnv1a_text(rank_fingerprint, hash);
  hash = detail::fnv1a(payload(), hash);
  fingerprint_ = "fnv1a64:" + detail::hex64(hash);
}

Result<bool, ResourceError> ThreeWayTable::save(const std::filesystem::path &path) const {
  try {
    return detail::write_resource(path, resource_kind, format_version, fingerprint_, payload());
  } catch (const std::bad_alloc &) {
    return Result<bool, ResourceError>::failure(ResourceError::MemoryFailure);
  }
}

Result<ThreeWayTable, ResourceError> ThreeWayTable::load(const std::filesystem::path &path) {
  using Loaded = Result<ThreeWayTable, ResourceError>;
  try {
    const auto resource = detail::read_resource(path, resource_kind, format_version);
    if (!resource) {
      return Loaded::failure(resource.error());
    }
    const std::span<const std::uint8_t> bytes(resource.value().bytes);
    std::size_t position = 0U;
    std::uint64_t count = 0U;
    if (bytes.size() != payload_bytes() || !detail::read_little(bytes, position, count) ||
        count != three_way_entry_count) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
    ThreeWayTable table;
    for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
      auto &combo = table.representatives_[hand_class];
      if (!detail::read_little(bytes, position, combo) ||
          (combo != three_way_unbuilt_hero && combo != representative_of(hand_class))) {
        return Loaded::failure(ResourceError::IntegrityFailure);
      }
    }
    table.entries_.resize(static_cast<std::size_t>(count));
    for (auto &entry : table.entries_) {
      if (!detail::read_little(bytes, position, entry.pairs)) {
        return Loaded::failure(ResourceError::IntegrityFailure);
      }
      for (auto &cell : entry.cells) {
        if (!detail::read_little(bytes, position, cell)) {
          return Loaded::failure(ResourceError::IntegrityFailure);
        }
      }
    }
    if (position != bytes.size()) {
      return Loaded::failure(ResourceError::IntegrityFailure);
    }
    // A hero class left out of a subset build keeps all its entries zero.
    for (std::uint8_t hand_class = 0U; hand_class < class_count; ++hand_class) {
      if (table.hero_built(hand_class)) {
        continue;
      }
      const auto first =
          table.entries_.begin() + static_cast<std::ptrdiff_t>(entry_index(hand_class, 0U, 0U));
      if (std::any_of(first, first + class_count * class_count,
                      [](const ThreeWayEntry &entry) { return entry != ThreeWayEntry{}; })) {
        return Loaded::failure(ResourceError::IntegrityFailure);
      }
    }
    table.fingerprint_ = resource.value().fingerprint;
    return Loaded::success(std::move(table));
  } catch (const std::bad_alloc &) {
    return Loaded::failure(ResourceError::MemoryFailure);
  }
}

} // namespace gtosd::card_abstraction
