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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
using Clock = std::chrono::steady_clock;

std::uint64_t assertions = 0U;

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

// Synthetic opponent groups: classes ranked by id (pairs first, then suited,
// then offsuit). The clustering tests only need a deterministic grouping.
ca::OpponentGroups synthetic_groups() {
  std::array<std::uint8_t, 81> ranking{};
  std::array<double, 81> equity{};
  for (std::uint8_t index = 0U; index < ranking.size(); ++index) {
    ranking[index] = index;
    equity[index] = 1.0 - static_cast<double>(index) / 81.0;
  }
  return ca::OpponentGroups::from_ranking(ranking, equity, "synthetic_test_groups");
}

ca::ClusteringParameters parameters(const std::uint16_t capacity, const unsigned threads,
                                    const std::uint32_t restarts,
                                    const std::uint32_t maximum_iterations,
                                    const std::uint32_t screening_sample) {
  ca::ClusteringParameters result;
  result.capacity = capacity;
  result.threads = threads;
  result.restarts = restarts;
  result.screening_iterations = 6U;
  result.maximum_iterations = maximum_iterations;
  result.screening_sample = screening_sample;
  result.partition_seed = 0x5041'5254'0000'0001ULL;
  return result;
}

void check_common(const ca::BoardCatalog &catalog, const ca::BucketTable &table,
                  const ca::ClusteringDiagnostics &diagnostics, const std::uint32_t expected_rows,
                  const std::uint32_t expected_width, const std::uint64_t expected_weight,
                  const std::string_view street_name) {
  static_cast<void>(catalog);
  require(table.rows() == expected_rows && table.centroid_width() == expected_width,
          std::string(street_name) + ": table dimensions");
  require(table.buckets().size() == static_cast<std::size_t>(expected_rows) * ca::combo_count,
          std::string(street_name) + ": one bucket per row and combo");
  require(diagnostics.total_weight == expected_weight,
          std::string(street_name) + ": observation weight equals the physical count");
  std::uint64_t live = 0U;
  for (const auto bucket : table.buckets()) {
    if (bucket != ca::no_bucket) {
      require(bucket < table.capacity(), std::string(street_name) + ": bucket below capacity");
      ++live;
    }
  }
  require(live == diagnostics.observations, std::string(street_name) + ": live rows are clustered");
  for (std::uint16_t bucket = 0U; bucket < table.capacity(); ++bucket) {
    require(diagnostics.occupancy_rows[bucket] > 0U,
            std::string(street_name) + ": no empty bucket after relabeling");
  }
  for (std::size_t iteration = 1U; iteration < diagnostics.inertia_by_iteration.size(); ++iteration) {
    require(diagnostics.inertia_by_iteration[iteration] <=
                diagnostics.inertia_by_iteration[iteration - 1U] + 1e-9,
            std::string(street_name) + ": inertia does not increase across iterations");
  }
  require(diagnostics.chosen_restart < diagnostics.screening_inertia.size(),
          std::string(street_name) + ": chosen restart is one of the screened ones");
}

void check_cdf_centroids(const ca::BucketTable &table, const std::uint32_t total,
                         const std::string_view street_name) {
  const auto width = table.centroid_width();
  for (std::uint16_t bucket = 0U; bucket < table.capacity(); ++bucket) {
    const auto *centroid = table.centroids().data() + static_cast<std::size_t>(bucket) * width;
    for (std::uint32_t bin = 1U; bin < width; ++bin) {
      require(centroid[bin] >= centroid[bin - 1U],
              std::string(street_name) + ": centroid CDF is non-decreasing");
    }
    require(centroid[width - 1U] == total,
            std::string(street_name) + ": centroid CDF ends at the runout count");
  }
  // Relabeling by strength: mean equity of centroid non-decreasing in bucket id.
  double previous = -1.0;
  for (std::uint16_t bucket = 0U; bucket < table.capacity(); ++bucket) {
    const auto *centroid = table.centroids().data() + static_cast<std::size_t>(bucket) * width;
    double mean = 0.0;
    std::uint32_t last = 0U;
    for (std::uint32_t bin = 0U; bin < width; ++bin) {
      mean += static_cast<double>(centroid[bin] - last) * (bin + 0.5) / width;
      last = centroid[bin];
    }
    require(mean + 1e-9 >= previous, std::string(street_name) + ": buckets ordered by strength");
    previous = mean;
  }
}

void test_flop(const ca::BoardCatalog &catalog, const ca::FlopFeatureTable &features) {
  ca::ClusteringDiagnostics diagnostics;
  const auto started = Clock::now();
  const auto table = ca::BucketTable::build_flop(catalog, features,
                                                 parameters(32U, 8U, 3U, 15U, 100'000U), &diagnostics);
  require(table.has_value(), "flop buckets build");
  std::cout << "flop clustering: " << seconds_since(started) << " s, iterations "
            << diagnostics.iterations << ", inertia " << diagnostics.inertia_by_iteration.back()
            << '\n';
  check_common(catalog, table.value(), diagnostics, ca::canonical_flop_count,
               ca::equity_histogram_bins, static_cast<std::uint64_t>(ca::physical_flops) * 528U,
               "flop");
  check_cdf_centroids(table.value(), ca::flop_runout_count, "flop");

  // Thread-count independence and determinism.
  ca::ClusteringDiagnostics single_diagnostics;
  const auto single = ca::BucketTable::build_flop(catalog, features,
                                                  parameters(32U, 1U, 3U, 15U, 100'000U),
                                                  &single_diagnostics);
  require(single.has_value() && single.value() == table.value(),
          "flop buckets are identical with one and eight threads");
  require(single_diagnostics.inertia_by_iteration == diagnostics.inertia_by_iteration,
          "flop inertia trajectory is identical with one and eight threads");

  // Lookup consistency and suit invariance on physical observations.
  ca::DeterministicRandom random(0x4255'434B'4554'5331ULL);
  const auto &combos = ca::combo_table();
  for (std::uint32_t sample = 0U; sample < 500U; ++sample) {
    const auto history = catalog.sample_physical_history(random);
    std::uint16_t combo = 0U;
    const std::uint64_t flop_mask =
        history.flop[0].mask() | history.flop[1].mask() | history.flop[2].mask();
    do {
      combo = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while ((combos.masks[combo] & flop_mask) != 0U);
    const std::array<gtosd::CardId, 2> hand{card(combos.cards[combo][0]),
                                            card(combos.cards[combo][1])};
    const auto lookup = ca::lookup_flop_bucket(catalog, table.value(), history.flop, hand);
    require(lookup.has_value() && lookup.value().bucket != ca::no_bucket,
            "flop lookup returns a bucket for a live hand");
    require(lookup.value().bucket ==
                table.value().row(lookup.value().row_index)[lookup.value().combo],
            "flop lookup agrees with the row view");
    const auto &permutation =
        ca::all_suit_permutations()[random.uniform_below(ca::suit_permutation_count)];
    const std::array<gtosd::CardId, 3> permuted_flop{ca::permute_card(history.flop[0], permutation),
                                                     ca::permute_card(history.flop[1], permutation),
                                                     ca::permute_card(history.flop[2], permutation)};
    const std::array<gtosd::CardId, 2> permuted_hand{ca::permute_card(hand[0], permutation),
                                                     ca::permute_card(hand[1], permutation)};
    const auto permuted = ca::lookup_flop_bucket(catalog, table.value(), permuted_flop, permuted_hand);
    require(permuted.has_value() && permuted.value().bucket == lookup.value().bucket,
            "flop bucket is invariant under a joint suit permutation");
  }
  const std::array<gtosd::CardId, 3> flop{card(0U), card(4U), card(8U)};
  const std::array<gtosd::CardId, 2> overlapping{card(0U), card(12U)};
  require(!ca::lookup_flop_bucket(catalog, table.value(), flop, overlapping).has_value(),
          "hand overlapping the flop is rejected");

  // Persistence.
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_card_abstraction_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "flop_buckets_test.bin";
  require(table.value().save(path).has_value(), "flop buckets save");
  const auto loaded = ca::BucketTable::load(path);
  require(loaded.has_value() && loaded.value() == table.value(), "flop buckets round trip");
  std::vector<char> bytes;
  {
    std::ifstream input(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }
  bytes[bytes.size() / 2U] = static_cast<char>(bytes[bytes.size() / 2U] ^ 0x77);
  const auto corrupted = directory / "flop_buckets_corrupted.bin";
  {
    std::ofstream output(corrupted, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  require(!ca::BucketTable::load(corrupted).has_value(), "corrupted bucket table is rejected");

  // Capacity larger than distinct observations is rejected only when it
  // exceeds the observation count; a small valid capacity works.
  const auto tiny = ca::BucketTable::build_flop(catalog, features, parameters(2U, 2U, 1U, 4U, 0U));
  require(tiny.has_value() && tiny.value().capacity() == 2U, "two-bucket flop table builds");
  ca::ClusteringParameters invalid = parameters(0U, 1U, 1U, 1U, 0U);
  require(!ca::BucketTable::build_flop(catalog, features, invalid).has_value(),
          "zero capacity is rejected");
}

void test_turn(const ca::BoardCatalog &catalog, const ca::TurnFeatureTable &features) {
  ca::ClusteringDiagnostics diagnostics;
  const auto started = Clock::now();
  const auto table = ca::BucketTable::build_turn(catalog, features,
                                                 parameters(64U, 8U, 2U, 6U, 200'000U), &diagnostics);
  require(table.has_value(), "turn buckets build");
  std::cout << "turn clustering: " << seconds_since(started) << " s, iterations "
            << diagnostics.iterations << ", inertia " << diagnostics.inertia_by_iteration.back()
            << '\n';
  check_common(catalog, table.value(), diagnostics, ca::canonical_flop_turn_count,
               ca::equity_histogram_bins,
               static_cast<std::uint64_t>(ca::physical_flop_turns) * 496U, "turn");
  check_cdf_centroids(table.value(), ca::turn_runout_count, "turn");
  ca::DeterministicRandom random(0x4255'434B'4554'5332ULL);
  const auto &combos = ca::combo_table();
  for (std::uint32_t sample = 0U; sample < 300U; ++sample) {
    const auto history = catalog.sample_physical_history(random);
    const std::uint64_t dead = history.flop[0].mask() | history.flop[1].mask() |
                               history.flop[2].mask() | history.turn.mask();
    std::uint16_t combo = 0U;
    do {
      combo = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while ((combos.masks[combo] & dead) != 0U);
    const std::array<gtosd::CardId, 2> hand{card(combos.cards[combo][0]),
                                            card(combos.cards[combo][1])};
    const auto lookup =
        ca::lookup_turn_bucket(catalog, table.value(), history.flop, history.turn, hand);
    require(lookup.has_value() && lookup.value().bucket != ca::no_bucket, "turn lookup works");
    const auto &permutation =
        ca::all_suit_permutations()[random.uniform_below(ca::suit_permutation_count)];
    const std::array<gtosd::CardId, 3> permuted_flop{ca::permute_card(history.flop[0], permutation),
                                                     ca::permute_card(history.flop[1], permutation),
                                                     ca::permute_card(history.flop[2], permutation)};
    const auto permuted = ca::lookup_turn_bucket(
        catalog, table.value(), permuted_flop, ca::permute_card(history.turn, permutation),
        {ca::permute_card(hand[0], permutation), ca::permute_card(hand[1], permutation)});
    require(permuted.has_value() && permuted.value().bucket == lookup.value().bucket,
            "turn bucket is invariant under a joint suit permutation");
  }
}

void test_river(const ca::BoardCatalog &catalog, const ca::RiverFeatureTable &features) {
  ca::ClusteringDiagnostics diagnostics;
  const auto started = Clock::now();
  const auto table = ca::BucketTable::build_river(catalog, features,
                                                  parameters(64U, 8U, 2U, 6U, 200'000U), &diagnostics);
  require(table.has_value(), "river buckets build");
  std::cout << "river clustering: " << seconds_since(started) << " s, iterations "
            << diagnostics.iterations << ", inertia " << diagnostics.inertia_by_iteration.back()
            << '\n';
  check_common(catalog, table.value(), diagnostics, ca::canonical_river_board_count,
               ca::river_feature_count,
               static_cast<std::uint64_t>(ca::physical_river_boards) * 465U, "river");
  std::uint16_t previous = 0U;
  for (std::uint16_t bucket = 0U; bucket < table.value().capacity(); ++bucket) {
    const auto strength =
        table.value().centroids()[static_cast<std::size_t>(bucket) * ca::river_feature_count];
    require(strength >= previous, "river buckets ordered by centroid equity");
    previous = strength;
  }
  ca::DeterministicRandom random(0x4255'434B'4554'5333ULL);
  const auto &combos = ca::combo_table();
  for (std::uint32_t sample = 0U; sample < 300U; ++sample) {
    const auto history = catalog.sample_physical_history(random);
    const std::array<gtosd::CardId, 5> board{history.flop[0], history.flop[1], history.flop[2],
                                             history.turn, history.river};
    std::uint64_t dead = 0U;
    for (const auto value : board) {
      dead |= value.mask();
    }
    std::uint16_t combo = 0U;
    do {
      combo = static_cast<std::uint16_t>(random.uniform_below(ca::combo_count));
    } while ((combos.masks[combo] & dead) != 0U);
    const std::array<gtosd::CardId, 2> hand{card(combos.cards[combo][0]),
                                            card(combos.cards[combo][1])};
    const auto lookup = ca::lookup_river_bucket(catalog, table.value(), board, hand);
    require(lookup.has_value() && lookup.value().bucket != ca::no_bucket, "river lookup works");
    const auto &permutation =
        ca::all_suit_permutations()[random.uniform_below(ca::suit_permutation_count)];
    std::array<gtosd::CardId, 5> permuted_board{};
    for (std::size_t index = 0U; index < board.size(); ++index) {
      permuted_board[index] = ca::permute_card(board[index], permutation);
    }
    const auto permuted = ca::lookup_river_bucket(
        catalog, table.value(), permuted_board,
        {ca::permute_card(hand[0], permutation), ca::permute_card(hand[1], permutation)});
    require(permuted.has_value() && permuted.value().bucket == lookup.value().bucket,
            "river bucket is invariant under a joint suit permutation");
    // The same hand on a different order of the same five cards reaches the same bucket.
    const std::array<gtosd::CardId, 5> reordered{board[4], board[2], board[0], board[3], board[1]};
    const auto reordered_lookup = ca::lookup_river_bucket(catalog, table.value(), reordered, hand);
    require(reordered_lookup.has_value() && reordered_lookup.value().bucket == lookup.value().bucket,
            "river bucket ignores the order of the five board cards");
  }
}

} // namespace

int main() {
  try {
    const auto started = Clock::now();
    const auto catalog = ca::BoardCatalog::build();
    const auto ranks = ca::RankTable::build();
    require(ranks.has_value(), "rank table builds");
    const auto flop_features = ca::FlopFeatureTable::build(catalog, ranks.value(), 8U);
    const auto turn_features = ca::TurnFeatureTable::build(catalog, ranks.value(), 8U);
    const auto river_features =
        ca::RiverFeatureTable::build(catalog, ranks.value(), synthetic_groups(), 8U);
    require(flop_features.has_value() && turn_features.has_value() && river_features.has_value(),
            "feature tables build");
    std::cout << "features ready after " << seconds_since(started) << " s\n";
    test_flop(catalog, flop_features.value());
    test_turn(catalog, turn_features.value());
    test_river(catalog, river_features.value());
    std::cout << "CARD_ABSTRACTION_BUCKETS=PASS assertions=" << assertions
              << " total_seconds=" << seconds_since(started) << '\n';
  } catch (const std::exception &error) {
    std::cerr << "CARD_ABSTRACTION_BUCKETS=FAIL " << error.what() << '\n';
    return 1;
  }
  return 0;
}
