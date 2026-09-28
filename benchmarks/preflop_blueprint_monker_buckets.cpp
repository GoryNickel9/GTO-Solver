// Builds MonkerSolver-style per-board bucket tables in the BucketTable format.
//
// On every canonical board the live combos are ranked by their exact river
// equity against a uniformly random disjoint opponent. Flop and turn use the
// mean over all runouts (strength level) and its variance (potential tier,
// board-level quantiles); the river uses the equity itself. Ids are
// level * tiers + tier on flop and turn and the level on the river, so equal
// values (in particular suit-isomorphic combos) always share an id. The
// centroid rows the format requires describe each id in the k-means feature
// spaces: the weighted median equity CDF (16 bins) on flop and turn and the
// weighted mean of the nine river features on the river. Resources are loaded
// from --resources-dir (written by gtosd_preflop_blueprint_resources).

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
using Clock = std::chrono::steady_clock;

// Every river showdown faces the 465 live hands minus the 59 sharing a card
// with the hero; an equity is kept exactly as 2 * wins + ties out of 812.
constexpr std::uint32_t river_opponent_count = 406U;
constexpr std::uint32_t flop_live_combos = 528U;
constexpr std::uint32_t turn_live_combos = 496U;
constexpr std::uint32_t river_live_combos = 465U;
constexpr std::size_t flop_card_count = 3U;
constexpr std::size_t river_board_card_count = 5U;
constexpr std::uint32_t default_levels = 30U;
constexpr std::uint32_t default_tiers = 4U;
constexpr unsigned default_threads = 8U;
constexpr unsigned maximum_threads = 64U;
constexpr std::uint32_t lookup_histories = 10'000U;
constexpr std::uint64_t lookup_seed = 0x4D4F'4E4B'4552'4C4BULL;

struct Settings {
  std::uint32_t levels{default_levels};
  std::uint32_t tiers{default_tiers};
  unsigned threads{default_threads};
};

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

std::uint64_t parse_bounded(const std::string_view value, const std::uint64_t minimum,
                            const std::uint64_t maximum) {
  std::size_t consumed = 0U;
  const auto parsed = std::stoull(std::string{value}, &consumed, 10);
  if (consumed != value.size() || parsed < minimum || parsed > maximum) {
    throw std::runtime_error("invalid number: " + std::string{value});
  }
  return parsed;
}

std::uint64_t mask_of(const std::span<const std::uint8_t> cards) noexcept {
  std::uint64_t mask = 0U;
  for (const auto card : cards) {
    mask |= std::uint64_t{1} << card;
  }
  return mask;
}

struct SelfChecks {
  std::uint64_t live_observations{0U};
  // Live combos without an id below the capacity, overlapping combos with one.
  std::uint64_t invalid_ids{0U};
  std::uint64_t live_count_mismatches{0U};
  std::uint64_t runout_mismatches{0U};
  std::uint64_t opponent_mismatches{0U};
  std::uint64_t symmetric_boards{0U};
  std::uint64_t orbit_pairs{0U};
  std::uint64_t orbit_mismatches{0U};
  // River only: exact equity differing from river_features coordinate 0.
  std::uint64_t feature_mismatches{0U};

  void merge(const SelfChecks &other) noexcept {
    live_observations += other.live_observations;
    invalid_ids += other.invalid_ids;
    live_count_mismatches += other.live_count_mismatches;
    runout_mismatches += other.runout_mismatches;
    opponent_mismatches += other.opponent_mismatches;
    symmetric_boards += other.symmetric_boards;
    orbit_pairs += other.orbit_pairs;
    orbit_mismatches += other.orbit_mismatches;
    feature_mismatches += other.feature_mismatches;
  }

  [[nodiscard]] bool passed() const noexcept {
    return live_observations > 0U && invalid_ids == 0U && live_count_mismatches == 0U &&
           runout_mismatches == 0U && opponent_mismatches == 0U && orbit_pairs > 0U &&
           orbit_mismatches == 0U && feature_mismatches == 0U;
  }
};

// Per-worker accumulators, merged in integers so the tables do not depend on
// the number of threads.
struct WorkerState {
  SelfChecks checks;
  std::uint64_t board_evaluations{0U};
  // Board multiplicity per id.
  std::vector<std::uint64_t> id_weights;
  // Flop/turn: weight of each cumulative count per (id, bin); river: weighted
  // feature sums per (id, feature).
  std::vector<std::uint64_t> centroid_statistics;
};

struct StreetOutput {
  std::vector<std::uint16_t> buckets;
  std::vector<std::uint16_t> centroids;
  std::vector<std::uint16_t> distinct_ids;
  SelfChecks checks;
  std::uint64_t board_evaluations{0U};
  std::uint32_t occupied_ids{0U};
  double seconds{0.0};
};

template <typename Body>
void parallel_boards(const std::size_t count, const unsigned threads, Body &&body) {
  std::vector<std::string> failures(threads);
  std::vector<std::thread> workers;
  workers.reserve(threads);
  try {
    for (unsigned worker = 0U; worker < threads; ++worker) {
      const auto first = count * worker / threads;
      const auto last = count * (worker + 1U) / threads;
      workers.emplace_back([first, last, worker, &body, &failures] {
        try {
          body(worker, first, last);
        } catch (const std::exception &error) {
          try {
            failures[worker] = error.what();
          } catch (...) {
            failures[worker] = "worker failed";
          }
        } catch (...) {
          failures[worker] = "worker failed";
        }
      });
    }
  } catch (...) {
    for (auto &thread : workers) {
      thread.join();
    }
    throw;
  }
  for (auto &thread : workers) {
    thread.join();
  }
  for (const auto &failure : failures) {
    if (!failure.empty()) {
      throw std::runtime_error("worker failed: " + failure);
    }
  }
}

// Sums every worker's accumulators into the first worker's state.
const WorkerState &merge_states(std::vector<WorkerState> &states) {
  auto &merged = states.front();
  for (std::size_t worker = 1U; worker < states.size(); ++worker) {
    const auto &state = states[worker];
    merged.checks.merge(state.checks);
    merged.board_evaluations += state.board_evaluations;
    for (std::size_t index = 0U; index < merged.id_weights.size(); ++index) {
      merged.id_weights[index] += state.id_weights[index];
    }
    for (std::size_t index = 0U; index < merged.centroid_statistics.size(); ++index) {
      merged.centroid_statistics[index] += state.centroid_statistics[index];
    }
  }
  return merged;
}

// Scales the rank of every value (the number of strictly smaller values among
// the n values) into `classes` classes: min(classes - 1,
// floor(classes * smaller / n)). Equal values share a class.
void quantile_classes(const std::vector<std::uint64_t> &values, const std::uint32_t classes,
                      std::vector<std::uint32_t> &order, std::vector<std::uint16_t> &output) {
  const auto count = values.size();
  order.resize(count);
  std::iota(order.begin(), order.end(), std::uint32_t{0});
  std::sort(order.begin(), order.end(),
            [&values](const std::uint32_t left, const std::uint32_t right) {
              return values[left] < values[right];
            });
  output.resize(count);
  std::size_t smaller = 0U;
  for (std::size_t position = 0U; position < count; ++position) {
    if (position > 0U && values[order[position]] != values[order[position - 1U]]) {
      smaller = position;
    }
    const auto scaled = static_cast<std::uint64_t>(classes) * smaller / count;
    output[order[position]] =
        static_cast<std::uint16_t>(std::min<std::uint64_t>(classes - 1U, scaled));
  }
}

// Card and combo images under the 24 suit permutations.
struct SymmetryMaps {
  std::array<bool, ca::suit_permutation_count> identity{};
  std::array<std::array<std::uint8_t, ca::deck_cards>, ca::suit_permutation_count> cards{};
  std::array<std::array<std::uint16_t, ca::combo_count>, ca::suit_permutation_count> combos{};
};

SymmetryMaps make_symmetry_maps() {
  SymmetryMaps maps;
  const auto &permutations = ca::all_suit_permutations();
  const auto &combos = ca::combo_table();
  for (std::size_t permutation = 0U; permutation < permutations.size(); ++permutation) {
    maps.identity[permutation] = permutations[permutation] == ca::identity_permutation;
    for (std::uint8_t card = 0U; card < ca::deck_cards; ++card) {
      maps.cards[permutation][card] =
          ca::permute_card(gtosd::CardId::from_index(card).value(), permutations[permutation])
              .value();
    }
    for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
      const auto &pair = combos.cards[combo];
      maps.combos[permutation][combo] = ca::combo_index(
          gtosd::CardId::from_index(maps.cards[permutation][pair[0]]).value(),
          gtosd::CardId::from_index(maps.cards[permutation][pair[1]]).value());
    }
  }
  return maps;
}

// Suit permutations other than the identity that map a canonical board onto
// itself as BoardCatalog canonicalizes it: flop and river boards as card sets,
// a flop+turn board as a flop set plus a fixed turn card.
void board_stabilizer(const SymmetryMaps &maps, const std::span<const std::uint8_t> set_cards,
                      const std::span<const std::uint8_t> fixed_cards,
                      std::vector<std::size_t> &output) {
  output.clear();
  const auto set_mask = mask_of(set_cards);
  for (std::size_t permutation = 0U; permutation < ca::suit_permutation_count; ++permutation) {
    if (maps.identity[permutation]) {
      continue;
    }
    std::uint64_t mapped = 0U;
    for (const auto card : set_cards) {
      mapped |= std::uint64_t{1} << maps.cards[permutation][card];
    }
    bool stabilizes = mapped == set_mask;
    for (const auto card : fixed_cards) {
      stabilizes = stabilizes && maps.cards[permutation][card] == card;
    }
    if (stabilizes) {
      output.push_back(permutation);
    }
  }
}

// Combos in the same suit-isomorphism orbit of the board must share their id.
void check_orbits(const SymmetryMaps &maps, const std::vector<std::size_t> &stabilizer,
                  const std::uint16_t *row, SelfChecks &checks) {
  if (stabilizer.empty()) {
    return;
  }
  ++checks.symmetric_boards;
  for (const auto permutation : stabilizer) {
    for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
      if (row[combo] == ca::no_bucket) {
        continue;
      }
      ++checks.orbit_pairs;
      if (row[maps.combos[permutation][combo]] != row[combo]) {
        ++checks.orbit_mismatches;
      }
    }
  }
}

// Every live combo must hold an id below the capacity and every overlapping
// combo no_bucket; returns the number of distinct ids on the board.
std::uint16_t verify_row(const std::uint16_t *row, const std::uint64_t board_mask,
                         const std::uint32_t capacity, std::vector<std::uint8_t> &seen,
                         SelfChecks &checks) {
  const auto &combos = ca::combo_table();
  std::fill(seen.begin(), seen.end(), std::uint8_t{0});
  std::uint16_t distinct = 0U;
  for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
    const auto bucket = row[combo];
    if ((combos.masks[combo] & board_mask) != 0U) {
      if (bucket != ca::no_bucket) {
        ++checks.invalid_ids;
      }
      continue;
    }
    ++checks.live_observations;
    if (bucket >= capacity) {
      ++checks.invalid_ids;
      continue;
    }
    if (seen[bucket] == 0U) {
      seen[bucket] = 1U;
      ++distinct;
    }
  }
  return distinct;
}

struct BoardScratch {
  ca::LiveHands live;
  std::vector<std::uint16_t> ranks;
  std::vector<ca::HandOutcomeCounts> outcomes;

  void evaluate(const ca::RankTable &table, const std::array<std::uint8_t, 5> &board) {
    ca::collect_live_hands(mask_of(board), live);
    ranks.resize(live.cards.size());
    outcomes.assign(live.cards.size(), ca::HandOutcomeCounts{});
    for (std::size_t hand = 0U; hand < live.cards.size(); ++hand) {
      ranks[hand] = table.rank_of(live.cards[hand], board);
    }
    ca::count_showdown_outcomes(live.cards, ranks, outcomes);
  }
};

// Exact moments of a combo's river equity over the runouts of one board. Each
// runout contributes s = 2 * wins + ties (the equity times 812), so over R
// runouts E = sum / (812 R) and V = (R * sum_squares - sum^2) / (812 R)^2.
struct ComboMoments {
  std::uint32_t runouts{0U};
  std::uint64_t sum{0U};
  std::uint64_t sum_squares{0U};
  std::array<std::uint16_t, ca::equity_histogram_bins> histogram{};
};

struct PreRiverBoard {
  // Known cards first; the remaining slots receive each runout.
  std::array<std::uint8_t, river_board_card_count> cards{};
  std::size_t known{0U};
  std::uint64_t mask{0U};
  std::uint32_t multiplicity{0U};
};

PreRiverBoard pre_river_board(const ca::BoardCatalog &catalog, const ca::BucketStreet street,
                              const std::size_t index) {
  PreRiverBoard board;
  if (street == ca::BucketStreet::Flop) {
    const auto &entry = catalog.flops()[index];
    for (std::size_t card = 0U; card < flop_card_count; ++card) {
      board.cards[card] = entry.cards[card].value();
    }
    board.known = flop_card_count;
    board.multiplicity = entry.multiplicity;
  } else {
    const auto &entry = catalog.flop_turns()[index];
    for (std::size_t card = 0U; card < flop_card_count; ++card) {
      board.cards[card] = entry.flop[card].value();
    }
    board.cards[flop_card_count] = entry.turn.value();
    board.known = flop_card_count + 1U;
    board.multiplicity = entry.multiplicity;
  }
  board.mask = mask_of(std::span<const std::uint8_t>(board.cards.data(), board.known));
  return board;
}

void accumulate_runouts(const ca::RankTable &ranks, const PreRiverBoard &board,
                        BoardScratch &scratch, std::vector<ComboMoments> &moments,
                        WorkerState &state) {
  std::fill(moments.begin(), moments.end(), ComboMoments{});
  const auto evaluate = [&](const std::array<std::uint8_t, river_board_card_count> &cards) {
    scratch.evaluate(ranks, cards);
    ++state.board_evaluations;
    for (std::size_t hand = 0U; hand < scratch.live.cards.size(); ++hand) {
      const auto &outcome = scratch.outcomes[hand];
      if (outcome.total() != river_opponent_count) {
        ++state.checks.opponent_mismatches;
      }
      const auto doubled_equity = 2U * static_cast<std::uint32_t>(outcome.wins) + outcome.ties;
      auto &entry = moments[scratch.live.combo_ids[hand]];
      ++entry.runouts;
      entry.sum += doubled_equity;
      entry.sum_squares += static_cast<std::uint64_t>(doubled_equity) * doubled_equity;
      ++entry.histogram[ca::equity_bin(outcome.equity())];
    }
  };
  auto cards = board.cards;
  const auto live_card = [&board](const std::uint8_t card) {
    return ((board.mask >> card) & 1U) == 0U;
  };
  if (board.known == flop_card_count) {
    for (std::uint8_t turn = 0U; turn < ca::deck_cards; ++turn) {
      if (!live_card(turn)) {
        continue;
      }
      for (auto river = static_cast<std::uint8_t>(turn + 1U); river < ca::deck_cards; ++river) {
        if (!live_card(river)) {
          continue;
        }
        cards[flop_card_count] = turn;
        cards[flop_card_count + 1U] = river;
        evaluate(cards);
      }
    }
  } else {
    for (std::uint8_t river = 0U; river < ca::deck_cards; ++river) {
      if (!live_card(river)) {
        continue;
      }
      cards[flop_card_count + 1U] = river;
      evaluate(cards);
    }
  }
}

// Coordinate-wise weighted median of the cumulative histogram counts, the
// exact L1 barycenter the k-means flop/turn tables store as centroids. Ids
// that no board uses keep a zero row.
std::vector<std::uint16_t> cdf_centroids(const WorkerState &merged, const std::uint32_t capacity,
                                         const std::size_t cdf_values) {
  std::vector<std::uint16_t> centroids(
      static_cast<std::size_t>(capacity) * ca::equity_histogram_bins, 0U);
  for (std::uint32_t id = 0U; id < capacity; ++id) {
    const auto weight = merged.id_weights[id];
    if (weight == 0U) {
      continue;
    }
    const auto half = (weight + 1U) / 2U;
    for (std::size_t bin = 0U; bin < ca::equity_histogram_bins; ++bin) {
      const auto slot = static_cast<std::size_t>(id) * ca::equity_histogram_bins + bin;
      const auto *weights = merged.centroid_statistics.data() + slot * cdf_values;
      std::uint64_t cumulative = 0U;
      std::size_t value = 0U;
      while (value + 1U < cdf_values) {
        cumulative += weights[value];
        if (cumulative >= half) {
          break;
        }
        ++value;
      }
      centroids[slot] = static_cast<std::uint16_t>(value);
    }
  }
  return centroids;
}

// Rounded weighted mean of the nine river features, the centroid the k-means
// river table stores. Ids that no board uses keep a zero row.
std::vector<std::uint16_t> river_centroids(const WorkerState &merged,
                                           const std::uint32_t capacity) {
  std::vector<std::uint16_t> centroids(
      static_cast<std::size_t>(capacity) * ca::river_feature_count, 0U);
  for (std::uint32_t id = 0U; id < capacity; ++id) {
    const auto weight = merged.id_weights[id];
    if (weight == 0U) {
      continue;
    }
    for (std::size_t feature = 0U; feature < ca::river_feature_count; ++feature) {
      const auto slot = static_cast<std::size_t>(id) * ca::river_feature_count + feature;
      centroids[slot] =
          static_cast<std::uint16_t>((merged.centroid_statistics[slot] + weight / 2U) / weight);
    }
  }
  return centroids;
}

const WorkerState &finish_output(StreetOutput &output, std::vector<WorkerState> &states) {
  const auto &merged = merge_states(states);
  output.checks = merged.checks;
  output.board_evaluations = merged.board_evaluations;
  output.occupied_ids = static_cast<std::uint32_t>(std::count_if(
      merged.id_weights.begin(), merged.id_weights.end(),
      [](const std::uint64_t weight) { return weight > 0U; }));
  return merged;
}

StreetOutput build_pre_river(const ca::BucketStreet street, const ca::BoardCatalog &catalog,
                             const ca::RankTable &ranks, const SymmetryMaps &maps,
                             const Settings &settings) {
  const auto started = Clock::now();
  const bool flop = street == ca::BucketStreet::Flop;
  const auto rows = flop ? catalog.flops().size() : catalog.flop_turns().size();
  const auto expected_live = flop ? flop_live_combos : turn_live_combos;
  const auto expected_runouts = flop ? ca::flop_runout_count : ca::turn_runout_count;
  const auto capacity = settings.levels * settings.tiers;
  // Cumulative counts range over 0..runouts.
  const std::size_t cdf_values = static_cast<std::size_t>(expected_runouts) + 1U;
  StreetOutput output;
  output.buckets.assign(rows * ca::combo_count, ca::no_bucket);
  output.distinct_ids.assign(rows, 0U);
  std::vector<WorkerState> states(settings.threads);
  for (auto &state : states) {
    state.id_weights.assign(capacity, 0U);
    state.centroid_statistics.assign(
        static_cast<std::size_t>(capacity) * ca::equity_histogram_bins * cdf_values, 0U);
  }
  parallel_boards(rows, settings.threads,
                  [&](const unsigned worker, const std::size_t first, const std::size_t last) {
    auto &state = states[worker];
    BoardScratch scratch;
    ca::LiveHands board_live;
    std::vector<ComboMoments> moments(ca::combo_count);
    std::vector<std::uint64_t> strength;
    std::vector<std::uint64_t> potential;
    std::vector<std::uint16_t> level_of;
    std::vector<std::uint16_t> tier_of;
    std::vector<std::uint32_t> order;
    std::vector<std::size_t> stabilizer;
    std::vector<std::uint8_t> seen(capacity);
    for (std::size_t index = first; index < last; ++index) {
      const auto board = pre_river_board(catalog, street, index);
      accumulate_runouts(ranks, board, scratch, moments, state);
      ca::collect_live_hands(board.mask, board_live);
      const auto live_count = board_live.combo_ids.size();
      if (live_count != expected_live) {
        ++state.checks.live_count_mismatches;
      }
      strength.resize(live_count);
      potential.resize(live_count);
      for (std::size_t hand = 0U; hand < live_count; ++hand) {
        const auto &entry = moments[board_live.combo_ids[hand]];
        if (entry.runouts != expected_runouts) {
          ++state.checks.runout_mismatches;
        }
        strength[hand] = entry.sum;
        // R^2 * 812^2 * V, non-negative by Cauchy-Schwarz.
        potential[hand] = entry.runouts * entry.sum_squares - entry.sum * entry.sum;
      }
      quantile_classes(strength, settings.levels, order, level_of);
      quantile_classes(potential, settings.tiers, order, tier_of);
      auto *row = output.buckets.data() + index * ca::combo_count;
      for (std::size_t hand = 0U; hand < live_count; ++hand) {
        const auto combo = board_live.combo_ids[hand];
        const auto id = static_cast<std::uint16_t>(level_of[hand] * settings.tiers + tier_of[hand]);
        row[combo] = id;
        state.id_weights[id] += board.multiplicity;
        auto *weights = state.centroid_statistics.data() +
                        static_cast<std::size_t>(id) * ca::equity_histogram_bins * cdf_values;
        std::size_t cumulative = 0U;
        for (std::size_t bin = 0U; bin < ca::equity_histogram_bins; ++bin) {
          cumulative += moments[combo].histogram[bin];
          // A combo with the wrong runout count is already flagged; clamp to
          // stay inside the statistics.
          weights[bin * cdf_values + std::min(cumulative, cdf_values - 1U)] += board.multiplicity;
        }
      }
      output.distinct_ids[index] = verify_row(row, board.mask, capacity, seen, state.checks);
      board_stabilizer(maps, std::span<const std::uint8_t>(board.cards.data(), flop_card_count),
                       std::span<const std::uint8_t>(board.cards.data() + flop_card_count,
                                                     board.known - flop_card_count),
                       stabilizer);
      check_orbits(maps, stabilizer, row, state.checks);
    }
  });
  output.centroids = cdf_centroids(finish_output(output, states), capacity, cdf_values);
  output.seconds = seconds_since(started);
  return output;
}

StreetOutput build_river(const ca::BoardCatalog &catalog, const ca::RankTable &ranks,
                         const ca::RiverFeatureTable &features, const SymmetryMaps &maps,
                         const Settings &settings) {
  const auto started = Clock::now();
  const auto rows = catalog.river_boards().size();
  const auto capacity = settings.levels;
  StreetOutput output;
  output.buckets.assign(rows * ca::combo_count, ca::no_bucket);
  output.distinct_ids.assign(rows, 0U);
  std::vector<WorkerState> states(settings.threads);
  for (auto &state : states) {
    state.id_weights.assign(capacity, 0U);
    state.centroid_statistics.assign(static_cast<std::size_t>(capacity) * ca::river_feature_count,
                                     0U);
  }
  parallel_boards(rows, settings.threads,
                  [&](const unsigned worker, const std::size_t first, const std::size_t last) {
    auto &state = states[worker];
    BoardScratch scratch;
    std::vector<std::uint64_t> strength;
    std::vector<std::uint16_t> level_of;
    std::vector<std::uint32_t> order;
    std::vector<std::size_t> stabilizer;
    std::vector<std::uint8_t> seen(capacity);
    std::array<std::uint8_t, river_board_card_count> cards{};
    for (std::size_t index = first; index < last; ++index) {
      const auto &entry = catalog.river_boards()[index];
      for (std::size_t card = 0U; card < river_board_card_count; ++card) {
        cards[card] = entry.cards[card].value();
      }
      scratch.evaluate(ranks, cards);
      ++state.board_evaluations;
      const auto live_count = scratch.live.cards.size();
      if (live_count != river_live_combos) {
        ++state.checks.live_count_mismatches;
      }
      strength.resize(live_count);
      for (std::size_t hand = 0U; hand < live_count; ++hand) {
        const auto &outcome = scratch.outcomes[hand];
        if (outcome.total() != river_opponent_count) {
          ++state.checks.opponent_mismatches;
        }
        strength[hand] = 2U * static_cast<std::uint64_t>(outcome.wins) + outcome.ties;
      }
      quantile_classes(strength, settings.levels, order, level_of);
      auto *row = output.buckets.data() + index * ca::combo_count;
      const auto board_index = static_cast<std::uint32_t>(index);
      for (std::size_t hand = 0U; hand < live_count; ++hand) {
        const auto combo = scratch.live.combo_ids[hand];
        const auto id = level_of[hand];
        row[combo] = id;
        const auto values = features.features(board_index, combo);
        if (values[0] != ca::equity_fixed_point(scratch.outcomes[hand].equity())) {
          ++state.checks.feature_mismatches;
        }
        state.id_weights[id] += entry.multiplicity;
        for (std::size_t feature = 0U; feature < ca::river_feature_count; ++feature) {
          state.centroid_statistics[static_cast<std::size_t>(id) * ca::river_feature_count +
                                    feature] +=
              static_cast<std::uint64_t>(values[feature]) * entry.multiplicity;
        }
      }
      output.distinct_ids[index] =
          verify_row(row, mask_of(cards), capacity, seen, state.checks);
      board_stabilizer(maps, cards, std::span<const std::uint8_t>(), stabilizer);
      check_orbits(maps, stabilizer, row, state.checks);
    }
  });
  output.centroids = river_centroids(finish_output(output, states), capacity);
  output.seconds = seconds_since(started);
  return output;
}

struct LookupCheck {
  std::uint64_t histories{0U};
  std::uint64_t hands{0U};
  std::uint64_t failures{0U};
};

// Mirrors the acceptance test of BoardContext::build on sampled physical
// histories: each street's canonical row and suit permutation come from the
// catalog, every live hand is moved into that frame and must hold an id below
// the table's capacity.
LookupCheck check_physical_lookups(const ca::BoardCatalog &catalog,
                                   const std::array<const ca::BucketTable *, 3> &tables) {
  LookupCheck check;
  ca::DeterministicRandom random(lookup_seed);
  const auto &combos = ca::combo_table();
  for (std::uint32_t sample = 0U; sample < lookup_histories; ++sample) {
    const auto history = catalog.sample_physical_history(random);
    const std::array<gtosd::CardId, river_board_card_count> board{
        history.flop[0], history.flop[1], history.flop[2], history.turn, history.river};
    std::uint64_t board_mask = 0U;
    for (const auto card : board) {
      board_mask |= card.mask();
    }
    const std::array lookups{catalog.lookup_flop(history.flop),
                             catalog.lookup_flop_turn(history.flop, history.turn),
                             catalog.lookup_river_board(board)};
    ++check.histories;
    if (!lookups[0] || !lookups[1] || !lookups[2]) {
      ++check.failures;
      continue;
    }
    for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
      if ((combos.masks[combo] & board_mask) != 0U) {
        continue;
      }
      ++check.hands;
      const auto first = gtosd::CardId::from_index(combos.cards[combo][0]).value();
      const auto second = gtosd::CardId::from_index(combos.cards[combo][1]).value();
      for (std::size_t street = 0U; street < tables.size(); ++street) {
        const auto &lookup = lookups[street].value();
        const auto canonical = ca::combo_index(ca::permute_card(first, lookup.permutation),
                                               ca::permute_card(second, lookup.permutation));
        const auto bucket = tables[street]->bucket(lookup.index, canonical);
        if (bucket == ca::no_bucket || bucket >= tables[street]->capacity()) {
          ++check.failures;
        }
      }
    }
  }
  return check;
}

// No clustering ran: zero restarts and iterations mark a rule-based table.
ca::ClusteringParameters rule_parameters(const std::uint32_t capacity) {
  ca::ClusteringParameters parameters;
  parameters.capacity = static_cast<std::uint16_t>(capacity);
  parameters.restarts = 0U;
  parameters.screening_iterations = 0U;
  parameters.maximum_iterations = 0U;
  parameters.screening_sample = 0U;
  parameters.partition_seed = 0U;
  parameters.threads = 1U;
  return parameters;
}

std::string pre_river_recipe(const ca::BucketStreet street, const Settings &settings,
                             const std::string &rank_fingerprint) {
  return std::string{"gtosd.monker_buckets.v1|street="} + ca::bucket_street_name(street) +
         "|levels=" + std::to_string(settings.levels) + "|tiers=" +
         std::to_string(settings.tiers) +
         "|strength=mean_river_equity|potential=river_equity_variance"
         "|centroids=equity_cdf16_weighted_median|ranks=" +
         rank_fingerprint;
}

std::string river_recipe(const Settings &settings, const std::string &rank_fingerprint,
                         const std::string &features_fingerprint) {
  return "gtosd.monker_buckets.v1|street=river|levels=" + std::to_string(settings.levels) +
         "|strength=river_equity|centroids=river_features_weighted_mean|ranks=" +
         rank_fingerprint + "|river_features=" + features_fingerprint;
}

ca::BucketTable make_table(const ca::BucketStreet street, const ca::BoardCatalog &catalog,
                           const std::uint32_t capacity, StreetOutput &output,
                           const std::string &recipe) {
  auto table = ca::BucketTable::from_assignment(street, catalog, rule_parameters(capacity),
                                                std::move(output.buckets),
                                                std::move(output.centroids), recipe);
  if (!table) {
    throw std::runtime_error(std::string{"bucket table assembly failed for "} +
                             ca::bucket_street_name(street) + ": " +
                             ca::resource_error_name(table.error()));
  }
  return std::move(table.value());
}

struct Spread {
  std::uint16_t minimum{0U};
  double median{0.0};
  std::uint16_t maximum{0U};
};

// Unweighted over canonical boards; the median of an even count averages the
// two middle values.
Spread spread_of(std::vector<std::uint16_t> values) {
  Spread spread;
  if (values.empty()) {
    return spread;
  }
  std::sort(values.begin(), values.end());
  const auto middle = values.size() / 2U;
  spread.minimum = values.front();
  spread.maximum = values.back();
  spread.median = values.size() % 2U == 1U
                      ? static_cast<double>(values[middle])
                      : (static_cast<double>(values[middle - 1U]) + values[middle]) / 2.0;
  return spread;
}

void write_street(std::ostream &out, const std::string_view name, const ca::BucketTable &table,
                  const StreetOutput &output, const bool last) {
  const auto spread = spread_of(output.distinct_ids);
  const auto &checks = output.checks;
  out << "  \"" << name << "\": {\"fingerprint\": \"" << table.fingerprint()
      << "\", \"recipe\": \"" << table.feature_fingerprint()
      << "\", \"capacity\": " << table.capacity()
      << ", \"centroid_width\": " << table.centroid_width() << ", \"rows\": " << table.rows()
      << ", \"board_evaluations\": " << output.board_evaluations
      << ", \"live_observations\": " << checks.live_observations
      << ", \"distinct_ids_per_board\": {\"min\": " << spread.minimum
      << ", \"median\": " << spread.median << ", \"max\": " << spread.maximum << "}"
      << ", \"occupied_ids\": " << output.occupied_ids
      << ", \"invalid_ids\": " << checks.invalid_ids
      << ", \"live_count_mismatches\": " << checks.live_count_mismatches
      << ", \"runout_mismatches\": " << checks.runout_mismatches
      << ", \"opponent_mismatches\": " << checks.opponent_mismatches
      << ", \"symmetric_boards\": " << checks.symmetric_boards
      << ", \"orbit_pairs_checked\": " << checks.orbit_pairs
      << ", \"orbit_mismatches\": " << checks.orbit_mismatches
      << ", \"feature_mismatches\": " << checks.feature_mismatches
      << ", \"payload_bytes\": " << table.payload_bytes()
      << ", \"compute_seconds\": " << output.seconds << "}" << (last ? "\n" : ",\n");
}

bool write_text(const std::filesystem::path &path, const std::string &text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
  stream.close();
  return !stream.fail();
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path output_dir;
    Settings settings;
    settings.threads = std::min(default_threads, std::max(1U, std::thread::hardware_concurrency()));
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{name});
      }
      const std::string_view value = argv[++index];
      if (name == "--resources-dir") {
        resources_dir = value;
      } else if (name == "--output-dir") {
        output_dir = value;
      } else if (name == "--threads") {
        settings.threads = static_cast<unsigned>(parse_bounded(value, 1U, maximum_threads));
      } else if (name == "--levels") {
        settings.levels =
            static_cast<std::uint32_t>(parse_bounded(value, 1U, ca::maximum_bucket_capacity));
      } else if (name == "--tiers") {
        settings.tiers =
            static_cast<std::uint32_t>(parse_bounded(value, 1U, ca::maximum_bucket_capacity));
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    if (resources_dir.empty()) {
      throw std::runtime_error("--resources-dir is required");
    }
    const auto pre_river_capacity = settings.levels * settings.tiers;
    if (pre_river_capacity > ca::maximum_bucket_capacity) {
      throw std::runtime_error("levels * tiers exceeds the bucket capacity limit");
    }

    const auto started = Clock::now();
    auto phase = Clock::now();
    // Built like the trainer builds it (BoardContext looks rows up in that
    // catalog), so a stale catalog file cannot shift the row indices.
    const auto catalog = ca::BoardCatalog::build();
    const auto rank_resource = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
    if (!rank_resource) {
      throw std::runtime_error(std::string{"rank table load failed: "} +
                               ca::resource_error_name(rank_resource.error()));
    }
    const auto &ranks = rank_resource.value();
    const auto maps = make_symmetry_maps();
    const auto load_seconds = seconds_since(phase);

    auto flop_output = build_pre_river(ca::BucketStreet::Flop, catalog, ranks, maps, settings);
    auto turn_output = build_pre_river(ca::BucketStreet::Turn, catalog, ranks, maps, settings);

    phase = Clock::now();
    StreetOutput river_output;
    std::string river_features_fingerprint;
    double river_features_seconds = 0.0;
    {
      // Scoped so the 226 MB feature table is released before saving.
      const auto features = ca::RiverFeatureTable::load(resources_dir / "river_features_v1.bin");
      if (!features) {
        throw std::runtime_error(std::string{"river features load failed: "} +
                                 ca::resource_error_name(features.error()));
      }
      river_features_seconds = seconds_since(phase);
      river_features_fingerprint = features.value().fingerprint();
      river_output = build_river(catalog, ranks, features.value(), maps, settings);
    }

    const auto flop = make_table(ca::BucketStreet::Flop, catalog, pre_river_capacity, flop_output,
                                 pre_river_recipe(ca::BucketStreet::Flop, settings,
                                                  ranks.fingerprint()));
    const auto turn = make_table(ca::BucketStreet::Turn, catalog, pre_river_capacity, turn_output,
                                 pre_river_recipe(ca::BucketStreet::Turn, settings,
                                                  ranks.fingerprint()));
    const auto river =
        make_table(ca::BucketStreet::River, catalog, settings.levels, river_output,
                   river_recipe(settings, ranks.fingerprint(), river_features_fingerprint));

    phase = Clock::now();
    const auto lookups = check_physical_lookups(catalog, {&flop, &turn, &river});
    const auto lookup_seconds = seconds_since(phase);

    bool saved = false;
    bool verified = false;
    if (!output_dir.empty()) {
      std::filesystem::create_directories(output_dir);
      saved = flop.save(output_dir / "flop_buckets_v1.bin").has_value() &&
              turn.save(output_dir / "turn_buckets_v1.bin").has_value() &&
              river.save(output_dir / "river_buckets_v1.bin").has_value();
      const auto loaded_flop = ca::BucketTable::load(output_dir / "flop_buckets_v1.bin");
      const auto loaded_turn = ca::BucketTable::load(output_dir / "turn_buckets_v1.bin");
      const auto loaded_river = ca::BucketTable::load(output_dir / "river_buckets_v1.bin");
      verified = saved && loaded_flop && loaded_flop.value() == flop && loaded_turn &&
                 loaded_turn.value() == turn && loaded_river && loaded_river.value() == river;
    }

    std::ostringstream report;
    report << "{\n"
           << "  \"schema\": \"gtosd.preflop_blueprint_monker_buckets_report.v1\",\n"
           << "  \"threads\": " << settings.threads << ",\n"
           << "  \"levels\": " << settings.levels << ", \"tiers\": " << settings.tiers << ",\n"
           << "  \"catalog_fingerprint\": \"" << catalog.fingerprint() << "\",\n"
           << "  \"rank_fingerprint\": \"" << ranks.fingerprint() << "\",\n"
           << "  \"river_features_fingerprint\": \"" << river_features_fingerprint << "\",\n"
           << "  \"load_seconds\": " << load_seconds << ",\n"
           << "  \"river_features_load_seconds\": " << river_features_seconds << ",\n";
    write_street(report, "flop", flop, flop_output, false);
    write_street(report, "turn", turn, turn_output, false);
    write_street(report, "river", river, river_output, false);
    report << "  \"physical_lookups\": {\"histories\": " << lookups.histories
           << ", \"hands\": " << lookups.hands << ", \"failures\": " << lookups.failures
           << ", \"seconds\": " << lookup_seconds << "},\n"
           << "  \"output_dir\": \"" << output_dir.generic_string() << "\",\n"
           << "  \"saved\": " << (saved ? "true" : "false") << ",\n"
           << "  \"reload_verified\": " << (verified ? "true" : "false") << ",\n"
           << "  \"total_seconds\": " << seconds_since(started) << "\n}\n";
    const bool report_written =
        output_dir.empty() || write_text(output_dir / "monker_buckets_report.json", report.str());
    std::cout << report.str();
    const bool ok = flop_output.checks.passed() && turn_output.checks.passed() &&
                    river_output.checks.passed() && lookups.hands > 0U &&
                    lookups.failures == 0U && (output_dir.empty() || verified) && report_written;
    std::cout << "PREFLOP_BLUEPRINT_MONKER_BUCKETS=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_MONKER_BUCKETS=FAIL " << error.what() << '\n';
    return 1;
  }
}
