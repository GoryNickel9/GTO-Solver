#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/equity/seven_card_table.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
using Clock = std::chrono::steady_clock;

std::uint64_t assertions = 0U;
double aa_equity = 0.0;
constexpr unsigned build_threads = 8U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

gtosd::CardId card(const std::uint8_t value) { return gtosd::CardId::from_index(value).value(); }

std::array<gtosd::CardId, 7> cards_of(const std::array<std::uint8_t, 7> &values) {
  std::array<gtosd::CardId, 7> cards{};
  for (std::size_t index = 0U; index < values.size(); ++index) {
    cards[index] = card(values[index]);
  }
  return cards;
}

// Draws `count` distinct card indices, sorted.
template <std::size_t Count>
std::array<std::uint8_t, Count> draw_cards(ca::DeterministicRandom &random) {
  std::array<std::uint8_t, 36> deck{};
  for (std::uint8_t index = 0U; index < deck.size(); ++index) {
    deck[index] = index;
  }
  for (std::size_t drawn = 0U; drawn < Count; ++drawn) {
    const auto offset = random.uniform_below(static_cast<std::uint32_t>(deck.size() - drawn));
    std::swap(deck[drawn], deck[drawn + offset]);
  }
  std::array<std::uint8_t, Count> result{};
  std::copy_n(deck.begin(), Count, result.begin());
  std::sort(result.begin(), result.end());
  return result;
}

void test_rank_table(const ca::RankTable &ranks) {
  require(ranks.distinct_ranks() > 100U && ranks.distinct_ranks() < 65'535U,
          "ordinal rank count is plausible");
  ca::DeterministicRandom random(0x5241'4E4B'5441'424CULL);
  std::array<std::uint8_t, 7> previous = draw_cards<7>(random);
  for (std::uint32_t sample = 0U; sample < 200'000U; ++sample) {
    const auto current = draw_cards<7>(random);
    const auto oracle_previous = gtosd::evaluate_seven(cards_of(previous)).value();
    const auto oracle_current = gtosd::evaluate_seven(cards_of(current)).value();
    const auto rank_previous = ranks.rank_of_sorted(previous);
    const auto rank_current = ranks.rank_of_sorted(current);
    const bool oracle_less = oracle_previous < oracle_current;
    const bool oracle_equal = oracle_previous == oracle_current;
    require((rank_previous < rank_current) == oracle_less &&
                (rank_previous == rank_current) == oracle_equal,
            "ordinal ranks order seven-card sets like the exact evaluator");
    require(ca::subset_index(current) ==
                gtosd::seven_card_combination_index(cards_of(current)).value(),
            "colex index equals the seven-card table index");
    previous = current;
  }
  // Ruleset spot checks through the oracle categories.
  const std::array<std::uint8_t, 7> flush_set{
      gtosd::CardId::from_parts(gtosd::Rank::Six, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Eight, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Ten, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Queen, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Ace, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::King, gtosd::Suit::Hearts).value(),
      gtosd::CardId::from_parts(gtosd::Rank::King, gtosd::Suit::Diamonds).value()};
  const std::array<std::uint8_t, 7> full_house_set{
      gtosd::CardId::from_parts(gtosd::Rank::King, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::King, gtosd::Suit::Hearts).value(),
      gtosd::CardId::from_parts(gtosd::Rank::King, gtosd::Suit::Diamonds).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Ace, gtosd::Suit::Clubs).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Ace, gtosd::Suit::Diamonds).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Seven, gtosd::Suit::Clubs).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Nine, gtosd::Suit::Hearts).value()};
  auto sorted_flush = flush_set;
  auto sorted_full = full_house_set;
  std::sort(sorted_flush.begin(), sorted_flush.end());
  std::sort(sorted_full.begin(), sorted_full.end());
  require(gtosd::evaluate_seven(cards_of(sorted_flush)).value().category ==
                  gtosd::HandCategory::Flush &&
              gtosd::evaluate_seven(cards_of(sorted_full)).value().category ==
                  gtosd::HandCategory::FullHouse,
          "spot-check sets have the intended categories");
  require(ranks.rank_of_sorted(sorted_flush) > ranks.rank_of_sorted(sorted_full),
          "flush ranks above full house in the Short Deck ordinal table");
  const std::array<std::uint8_t, 7> wheel_set{
      gtosd::CardId::from_parts(gtosd::Rank::Ace, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Six, gtosd::Suit::Clubs).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Seven, gtosd::Suit::Diamonds).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Eight, gtosd::Suit::Hearts).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Nine, gtosd::Suit::Spades).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Jack, gtosd::Suit::Clubs).value(),
      gtosd::CardId::from_parts(gtosd::Rank::Queen, gtosd::Suit::Diamonds).value()};
  auto sorted_wheel = wheel_set;
  std::sort(sorted_wheel.begin(), sorted_wheel.end());
  require(gtosd::evaluate_seven(cards_of(sorted_wheel)).value().category ==
              gtosd::HandCategory::Straight,
          "A-6-7-8-9 is a straight");
  require(ranks.rank_of_sorted(sorted_wheel) < ranks.rank_of_sorted(sorted_full),
          "nine-high straight ranks below a full house");
}

void test_showdown_kernel(const ca::BoardCatalog &catalog, const ca::RankTable &ranks) {
  ca::DeterministicRandom random(0x5348'4F57'444F'574EULL);
  ca::LiveHands live;
  std::vector<std::uint16_t> hand_ranks;
  std::vector<ca::HandOutcomeCounts> sweep;
  std::vector<ca::HandOutcomeCounts> pairwise;
  for (std::uint32_t sample = 0U; sample < 300U; ++sample) {
    const auto history = catalog.sample_physical_history(random);
    std::array<std::uint8_t, 5> board{history.flop[0].value(), history.flop[1].value(),
                                      history.flop[2].value(), history.turn.value(),
                                      history.river.value()};
    std::sort(board.begin(), board.end());
    std::uint64_t mask = 0U;
    for (const auto value : board) {
      mask |= std::uint64_t{1} << value;
    }
    ca::collect_live_hands(mask, live);
    require(live.cards.size() == 465U, "465 live hands on a five-card board");
    hand_ranks.resize(live.cards.size());
    for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
      hand_ranks[hand] = ranks.rank_of(live.cards[hand], board);
    }
    sweep.assign(live.cards.size(), {});
    pairwise.assign(live.cards.size(), {});
    ca::count_showdown_outcomes(live.cards, hand_ranks, sweep);
    ca::count_showdown_outcomes_pairwise(live.cards, hand_ranks, live.cards, hand_ranks, pairwise);
    for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
      require(sweep[hand].wins == pairwise[hand].wins && sweep[hand].ties == pairwise[hand].ties &&
                  sweep[hand].losses == pairwise[hand].losses,
              "sweep kernel equals the pairwise reference");
      require(sweep[hand].total() == 406U, "every hero faces 406 disjoint opponents");
    }
  }
}

void test_all_in_table(const ca::AllInTable &all_in, const ca::RankTable &ranks) {
  const auto &combos = ca::combo_table();
  ca::DeterministicRandom random(0x414C'4C49'4E54'4142ULL);
  std::uint32_t disjoint_pairs = 0U;
  for (std::uint16_t a = 0U; a < ca::combo_count; ++a) {
    for (std::uint16_t b = static_cast<std::uint16_t>(a + 1U); b < ca::combo_count; ++b) {
      const auto &entry = all_in.entries()[ca::combo_pair_index(a, b)];
      if ((combos.masks[a] & combos.masks[b]) != 0U) {
        require(entry.total() == 0U, "overlapping pairs stay empty");
      } else {
        ++disjoint_pairs;
        require(entry.total() == ca::all_in_runout_count, "disjoint pair covers 201,376 runouts");
        const auto forward = all_in.outcome(a, b);
        const auto backward = all_in.outcome(b, a);
        require(forward.wins == backward.losses && forward.ties == backward.ties &&
                    forward.losses == backward.wins,
                "outcome orientation is antisymmetric");
      }
    }
  }
  require(disjoint_pairs == ca::disjoint_combo_pair_count, "176,715 disjoint combo pairs");

  // Direct enumeration through the rank table for 40 random disjoint pairs.
  for (std::uint32_t sample = 0U; sample < 40U; ++sample) {
    std::uint16_t a = 0U;
    std::uint16_t b = 0U;
    do {
      a = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
      b = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while (a == b || (combos.masks[a] & combos.masks[b]) != 0U);
    const auto dead = combos.masks[a] | combos.masks[b];
    std::array<std::uint8_t, 32> remaining{};
    std::size_t count = 0U;
    for (std::uint8_t value = 0U; value < 36U; ++value) {
      if ((dead & (std::uint64_t{1} << value)) == 0U) {
        remaining[count++] = value;
      }
    }
    ca::PairOutcome direct;
    std::array<std::uint8_t, 5> pick{};
    for (std::uint64_t index = 0U; index < ca::binomial(32U, 5U); ++index) {
      require(ca::subset_from_index(index, pick), "runout index decodes");
      const std::array<std::uint8_t, 5> board{remaining[pick[0]], remaining[pick[1]],
                                              remaining[pick[2]], remaining[pick[3]],
                                              remaining[pick[4]]};
      const auto rank_a = ranks.rank_of(combos.cards[a], board);
      const auto rank_b = ranks.rank_of(combos.cards[b], board);
      if (rank_a > rank_b) {
        ++direct.wins;
      } else if (rank_a == rank_b) {
        ++direct.ties;
      } else {
        ++direct.losses;
      }
    }
    require(all_in.outcome(a, b) == direct, "pair table equals direct enumeration");
  }

  // Three pairs against the exact oracle evaluator.
  for (std::uint32_t sample = 0U; sample < 3U; ++sample) {
    std::uint16_t a = 0U;
    std::uint16_t b = 0U;
    do {
      a = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
      b = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while (a == b || (combos.masks[a] & combos.masks[b]) != 0U);
    const auto dead = combos.masks[a] | combos.masks[b];
    std::vector<gtosd::CardId> remaining;
    for (std::uint8_t value = 0U; value < 36U; ++value) {
      if ((dead & (std::uint64_t{1} << value)) == 0U) {
        remaining.push_back(card(value));
      }
    }
    ca::PairOutcome oracle;
    std::array<std::uint8_t, 5> pick{};
    for (std::uint64_t index = 0U; index < ca::binomial(32U, 5U); ++index) {
      require(ca::subset_from_index(index, pick), "runout index decodes");
      const std::array<gtosd::CardId, 7> hero{card(combos.cards[a][0]), card(combos.cards[a][1]),
                                              remaining[pick[0]],        remaining[pick[1]],
                                              remaining[pick[2]],        remaining[pick[3]],
                                              remaining[pick[4]]};
      const std::array<gtosd::CardId, 7> villain{card(combos.cards[b][0]),
                                                 card(combos.cards[b][1]),
                                                 remaining[pick[0]],
                                                 remaining[pick[1]],
                                                 remaining[pick[2]],
                                                 remaining[pick[3]],
                                                 remaining[pick[4]]};
      const auto hero_value = gtosd::evaluate_seven(hero).value();
      const auto villain_value = gtosd::evaluate_seven(villain).value();
      if (hero_value > villain_value) {
        ++oracle.wins;
      } else if (hero_value == villain_value) {
        ++oracle.ties;
      } else {
        ++oracle.losses;
      }
    }
    require(all_in.outcome(a, b) == oracle, "pair table equals the exact oracle enumeration");
  }
}

void test_opponent_groups(const ca::OpponentGroups &groups) {
  std::uint32_t mass = 0U;
  for (const auto value : groups.group_mass) {
    require(value >= 48U && value <= 120U, "group masses are balanced");
    mass += value;
  }
  require(mass == ca::combo_count, "group masses sum to 630 combos");
  const auto &combos = ca::combo_table();
  static_cast<void>(combos);
  require(groups.group_of_class[0] == 0U, "AA belongs to the strongest group");
  for (std::uint8_t left = 0U; left < 81U; ++left) {
    for (std::uint8_t right = 0U; right < 81U; ++right) {
      if (groups.group_of_class[left] < groups.group_of_class[right]) {
        require(groups.class_equity[left] >= groups.class_equity[right],
                "stronger groups hold classes with higher all-in equity");
      }
    }
  }
  require(groups.class_equity[0] > 0.70 && groups.class_equity[0] < 0.95,
          "AA all-in equity against a random hand is plausible");
  require(std::max_element(groups.class_equity.begin(), groups.class_equity.end()) ==
              groups.class_equity.begin(),
          "AA has the highest all-in equity of the 81 classes");
  aa_equity = groups.class_equity[0];
}

std::uint32_t equity_bin_of(const ca::HandOutcomeCounts &counts) {
  return ca::equity_bin(counts.equity());
}

void test_flop_features(const ca::BoardCatalog &catalog, const ca::RankTable &ranks,
                        const ca::FlopFeatureTable &features) {
  ca::DeterministicRandom random(0x464C'4F50'4645'4154ULL);
  const auto &combos = ca::combo_table();
  ca::LiveHands live;
  std::vector<std::uint16_t> hand_ranks;
  std::vector<ca::HandOutcomeCounts> outcomes;
  for (std::uint32_t sample = 0U; sample < 300U; ++sample) {
    const auto flop_index = random.uniform_below(static_cast<std::uint32_t>(catalog.flops().size()));
    const auto &flop = catalog.flops()[flop_index].cards;
    const std::uint64_t flop_mask = flop[0].mask() | flop[1].mask() | flop[2].mask();
    std::uint16_t combo = 0U;
    do {
      combo = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while ((combos.masks[combo] & flop_mask) != 0U);
    std::array<std::uint32_t, ca::equity_histogram_bins> direct{};
    const auto dead = flop_mask | combos.masks[combo];
    std::uint32_t runouts = 0U;
    for (std::uint8_t turn = 0U; turn < 36U; ++turn) {
      if ((dead & (std::uint64_t{1} << turn)) != 0U) {
        continue;
      }
      for (std::uint8_t river = static_cast<std::uint8_t>(turn + 1U); river < 36U; ++river) {
        if ((dead & (std::uint64_t{1} << river)) != 0U) {
          continue;
        }
        std::array<std::uint8_t, 5> board{flop[0].value(), flop[1].value(), flop[2].value(), turn,
                                          river};
        std::sort(board.begin(), board.end());
        std::uint64_t board_mask = 0U;
        for (const auto value : board) {
          board_mask |= std::uint64_t{1} << value;
        }
        ca::collect_live_hands(board_mask, live);
        hand_ranks.resize(live.cards.size());
        for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
          hand_ranks[hand] = ranks.rank_of(live.cards[hand], board);
        }
        const std::array<std::array<std::uint8_t, 2>, 1> hero{combos.cards[combo]};
        const std::array<std::uint16_t, 1> hero_rank{ranks.rank_of(combos.cards[combo], board)};
        outcomes.assign(1U, {});
        ca::count_showdown_outcomes_pairwise(hero, hero_rank, live.cards, hand_ranks, outcomes);
        require(outcomes[0].total() == 406U, "hero faces 406 opponents on each runout");
        ++direct[equity_bin_of(outcomes[0])];
        ++runouts;
      }
    }
    require(runouts == ca::flop_runout_count, "465 runouts per flop observation");
    const auto stored = features.histogram(flop_index, combo);
    std::uint32_t sum = 0U;
    for (std::size_t bin = 0U; bin < ca::equity_histogram_bins; ++bin) {
      require(stored[bin] == direct[bin], "flop histogram equals the direct computation");
      sum += stored[bin];
    }
    require(sum == ca::flop_runout_count, "flop histogram sums to 465");
  }
  for (std::uint32_t sample = 0U; sample < 200U; ++sample) {
    const auto flop_index = random.uniform_below(static_cast<std::uint32_t>(catalog.flops().size()));
    const auto &flop = catalog.flops()[flop_index].cards;
    const std::uint64_t flop_mask = flop[0].mask() | flop[1].mask() | flop[2].mask();
    std::uint16_t combo = 0U;
    do {
      combo = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while ((combos.masks[combo] & flop_mask) == 0U);
    const auto stored = features.histogram(flop_index, combo);
    require(std::all_of(stored.begin(), stored.end(), [](const auto value) { return value == 0U; }),
            "overlapping combos hold empty histograms");
  }
  // Suit invariance through the catalog lookup.
  for (std::uint32_t sample = 0U; sample < 300U; ++sample) {
    const auto cards = draw_cards<5>(random);
    const std::array<gtosd::CardId, 3> flop{card(cards[0]), card(cards[1]), card(cards[2])};
    const std::array<gtosd::CardId, 2> hand{card(cards[3]), card(cards[4])};
    const auto lookup = catalog.lookup_flop(flop).value();
    const auto combo = ca::combo_index(ca::permute_card(hand[0], lookup.permutation),
                                       ca::permute_card(hand[1], lookup.permutation));
    const auto reference = features.histogram(lookup.index, combo);
    const auto &permutation =
        ca::all_suit_permutations()[random.uniform_below(ca::suit_permutation_count)];
    const std::array<gtosd::CardId, 3> permuted_flop{ca::permute_card(flop[0], permutation),
                                                     ca::permute_card(flop[1], permutation),
                                                     ca::permute_card(flop[2], permutation)};
    const auto permuted_lookup = catalog.lookup_flop(permuted_flop).value();
    const auto permuted_combo = ca::combo_index(
        ca::permute_card(ca::permute_card(hand[0], permutation), permuted_lookup.permutation),
        ca::permute_card(ca::permute_card(hand[1], permutation), permuted_lookup.permutation));
    require(permuted_lookup.index == lookup.index, "permuted flop maps to the same class");
    const auto permuted = features.histogram(permuted_lookup.index, permuted_combo);
    require(std::equal(reference.begin(), reference.end(), permuted.begin()),
            "flop histogram is invariant under a joint suit permutation of flop and hand");
  }
}

void test_turn_features(const ca::BoardCatalog &catalog, const ca::RankTable &ranks,
                        const ca::TurnFeatureTable &features) {
  ca::DeterministicRandom random(0x5455'524E'4645'4154ULL);
  const auto &combos = ca::combo_table();
  ca::LiveHands live;
  std::vector<std::uint16_t> hand_ranks;
  std::vector<ca::HandOutcomeCounts> outcomes;
  for (std::uint32_t sample = 0U; sample < 200U; ++sample) {
    const auto index =
        random.uniform_below(static_cast<std::uint32_t>(catalog.flop_turns().size()));
    const auto &entry = catalog.flop_turns()[index];
    const std::uint64_t dead_board = entry.flop[0].mask() | entry.flop[1].mask() |
                                     entry.flop[2].mask() | entry.turn.mask();
    std::uint16_t combo = 0U;
    do {
      combo = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while ((combos.masks[combo] & dead_board) != 0U);
    std::array<std::uint32_t, ca::equity_histogram_bins> direct{};
    const auto dead = dead_board | combos.masks[combo];
    std::uint32_t rivers = 0U;
    for (std::uint8_t river = 0U; river < 36U; ++river) {
      if ((dead & (std::uint64_t{1} << river)) != 0U) {
        continue;
      }
      std::array<std::uint8_t, 5> board{entry.flop[0].value(), entry.flop[1].value(),
                                        entry.flop[2].value(), entry.turn.value(), river};
      std::sort(board.begin(), board.end());
      std::uint64_t board_mask = 0U;
      for (const auto value : board) {
        board_mask |= std::uint64_t{1} << value;
      }
      ca::collect_live_hands(board_mask, live);
      hand_ranks.resize(live.cards.size());
      for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
        hand_ranks[hand] = ranks.rank_of(live.cards[hand], board);
      }
      const std::array<std::array<std::uint8_t, 2>, 1> hero{combos.cards[combo]};
      const std::array<std::uint16_t, 1> hero_rank{ranks.rank_of(combos.cards[combo], board)};
      outcomes.assign(1U, {});
      ca::count_showdown_outcomes_pairwise(hero, hero_rank, live.cards, hand_ranks, outcomes);
      ++direct[equity_bin_of(outcomes[0])];
      ++rivers;
    }
    require(rivers == ca::turn_runout_count, "30 rivers per turn observation");
    const auto stored = features.histogram(index, combo);
    std::uint32_t sum = 0U;
    for (std::size_t bin = 0U; bin < ca::equity_histogram_bins; ++bin) {
      require(stored[bin] == direct[bin], "turn histogram equals the direct computation");
      sum += stored[bin];
    }
    require(sum == ca::turn_runout_count, "turn histogram sums to 30");
  }
}

void test_river_features(const ca::BoardCatalog &catalog, const ca::RankTable &ranks,
                         const ca::OpponentGroups &groups, const ca::RiverFeatureTable &features) {
  ca::DeterministicRandom random(0x5249'5645'5246'4541ULL);
  const auto &combos = ca::combo_table();
  ca::LiveHands live;
  std::vector<std::uint16_t> hand_ranks;
  for (std::uint32_t sample = 0U; sample < 300U; ++sample) {
    const auto index =
        random.uniform_below(static_cast<std::uint32_t>(catalog.river_boards().size()));
    const auto &entry = catalog.river_boards()[index];
    std::array<std::uint8_t, 5> board{};
    std::uint64_t board_mask = 0U;
    for (std::size_t position = 0U; position < board.size(); ++position) {
      board[position] = entry.cards[position].value();
      board_mask |= entry.cards[position].mask();
    }
    ca::collect_live_hands(board_mask, live);
    hand_ranks.resize(live.cards.size());
    for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
      hand_ranks[hand] = ranks.rank_of(live.cards[hand], board);
    }
    const auto hero = random.uniform_below(static_cast<std::uint32_t>(live.cards.size()));
    std::array<ca::HandOutcomeCounts, ca::river_feature_count> direct{};
    for (std::size_t opponent = 0U; opponent < live.cards.size(); ++opponent) {
      const auto &h = live.cards[hero];
      const auto &o = live.cards[opponent];
      if (opponent == hero || o[0] == h[0] || o[0] == h[1] || o[1] == h[0] || o[1] == h[1]) {
        continue;
      }
      const auto group = 1U + groups.group_of_class[combos.hand_class[live.combo_ids[opponent]]];
      if (hand_ranks[hero] > hand_ranks[opponent]) {
        ++direct[0].wins;
        ++direct[group].wins;
      } else if (hand_ranks[hero] == hand_ranks[opponent]) {
        ++direct[0].ties;
        ++direct[group].ties;
      } else {
        ++direct[0].losses;
        ++direct[group].losses;
      }
    }
    require(direct[0].total() == 406U, "river hero faces 406 opponents");
    const auto stored = features.features(index, live.combo_ids[hero]);
    for (std::size_t feature = 0U; feature < ca::river_feature_count; ++feature) {
      require(stored[feature] == ca::equity_fixed_point(direct[feature].equity()),
              "river feature equals the direct computation");
    }
  }
}

void test_persistence(const ca::RankTable &ranks, const ca::AllInTable &all_in,
                      const ca::OpponentGroups &groups, const ca::FlopFeatureTable &flop,
                      const ca::TurnFeatureTable &turn, const ca::RiverFeatureTable &river) {
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_card_abstraction_tests";
  std::filesystem::create_directories(directory);
  const auto rank_path = directory / "rank_table_v1.bin";
  require(ranks.save(rank_path).has_value(), "rank table saves");
  const auto loaded_ranks = ca::RankTable::load(rank_path);
  require(loaded_ranks.has_value() && loaded_ranks.value() == ranks, "rank table round trip");
  const auto all_in_path = directory / "all_in_v1.bin";
  require(all_in.save(all_in_path).has_value(), "all-in table saves");
  const auto loaded_all_in = ca::AllInTable::load(all_in_path);
  require(loaded_all_in.has_value() && loaded_all_in.value() == all_in, "all-in round trip");
  const auto groups_path = directory / "groups_v1.bin";
  require(groups.save(groups_path).has_value(), "groups save");
  const auto loaded_groups = ca::OpponentGroups::load(groups_path);
  require(loaded_groups.has_value() &&
              loaded_groups.value().group_of_class == groups.group_of_class &&
              loaded_groups.value().group_mass == groups.group_mass &&
              loaded_groups.value().fingerprint == groups.fingerprint,
          "groups round trip");
  const auto flop_path = directory / "flop_features_v1.bin";
  require(flop.save(flop_path).has_value(), "flop features save");
  const auto loaded_flop = ca::FlopFeatureTable::load(flop_path);
  require(loaded_flop.has_value() && loaded_flop.value().counts() == flop.counts() &&
              loaded_flop.value().fingerprint() == flop.fingerprint(),
          "flop features round trip");
  const auto turn_path = directory / "turn_features_v1.bin";
  require(turn.save(turn_path).has_value(), "turn features save");
  const auto loaded_turn = ca::TurnFeatureTable::load(turn_path);
  require(loaded_turn.has_value() && loaded_turn.value().counts() == turn.counts() &&
              loaded_turn.value().fingerprint() == turn.fingerprint(),
          "turn features round trip");
  const auto river_path = directory / "river_features_v1.bin";
  require(river.save(river_path).has_value(), "river features save");
  const auto loaded_river = ca::RiverFeatureTable::load(river_path);
  require(loaded_river.has_value() && loaded_river.value().values() == river.values() &&
              loaded_river.value().fingerprint() == river.fingerprint(),
          "river features round trip");

  std::vector<char> bytes;
  {
    std::ifstream input(all_in_path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }
  bytes[bytes.size() / 3U] = static_cast<char>(bytes[bytes.size() / 3U] ^ 0x3C);
  const auto corrupted = directory / "all_in_corrupted.bin";
  {
    std::ofstream output(corrupted, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  const auto rejected = ca::AllInTable::load(corrupted);
  require(!rejected.has_value() && rejected.error() == ca::ResourceError::IntegrityFailure,
          "corrupted resource is rejected");
  require(!ca::RankTable::load(all_in_path).has_value(),
          "a resource of another kind is rejected");
}

} // namespace

int main() {
  try {
    const auto started = Clock::now();
    auto phase = Clock::now();
    const auto ranks = ca::RankTable::build();
    require(ranks.has_value(), "rank table builds");
    const auto rank_seconds = seconds_since(phase);
    test_rank_table(ranks.value());

    const auto catalog = ca::BoardCatalog::build();
    test_showdown_kernel(catalog, ranks.value());

    phase = Clock::now();
    const auto all_in = ca::AllInTable::build(ranks.value(), build_threads);
    require(all_in.has_value(), "all-in table builds");
    const auto all_in_seconds = seconds_since(phase);
    test_all_in_table(all_in.value(), ranks.value());

    const auto groups = ca::OpponentGroups::build(all_in.value());
    test_opponent_groups(groups);

    phase = Clock::now();
    const auto flop = ca::FlopFeatureTable::build(catalog, ranks.value(), build_threads);
    require(flop.has_value(), "flop features build");
    const auto flop_seconds = seconds_since(phase);
    test_flop_features(catalog, ranks.value(), flop.value());

    phase = Clock::now();
    const auto turn = ca::TurnFeatureTable::build(catalog, ranks.value(), build_threads);
    require(turn.has_value(), "turn features build");
    const auto turn_seconds = seconds_since(phase);
    test_turn_features(catalog, ranks.value(), turn.value());

    phase = Clock::now();
    const auto river = ca::RiverFeatureTable::build(catalog, ranks.value(), groups, build_threads);
    require(river.has_value(), "river features build");
    const auto river_seconds = seconds_since(phase);
    test_river_features(catalog, ranks.value(), groups, river.value());

    test_persistence(ranks.value(), all_in.value(), groups, flop.value(), turn.value(),
                     river.value());

    std::cout << "CARD_ABSTRACTION_FEATURES=PASS assertions=" << assertions
              << " distinct_ranks=" << ranks.value().distinct_ranks() << " aa_equity=" << aa_equity
              << " rank_seconds=" << rank_seconds << " all_in_seconds=" << all_in_seconds
              << " flop_seconds=" << flop_seconds << " turn_seconds=" << turn_seconds
              << " river_seconds=" << river_seconds << " total_seconds=" << seconds_since(started)
              << " rank_fingerprint=" << ranks.value().fingerprint()
              << " all_in_fingerprint=" << all_in.value().fingerprint()
              << " groups_fingerprint=" << groups.fingerprint
              << " flop_fingerprint=" << flop.value().fingerprint()
              << " turn_fingerprint=" << turn.value().fingerprint()
              << " river_fingerprint=" << river.value().fingerprint() << '\n';
  } catch (const std::exception &error) {
    std::cerr << "CARD_ABSTRACTION_FEATURES=FAIL " << error.what() << '\n';
    return 1;
  }
  return 0;
}
