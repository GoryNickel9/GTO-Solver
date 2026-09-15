// Builds the precomputed resources of the preflop blueprint solver (ordinal
// rank table, preflop all-in pair table, opponent groups, exact flop/turn/river
// features), reports timings, sizes and fingerprints, and optionally writes
// the versioned files to a directory and verifies them by reloading.

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/equity/evaluator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

namespace ca = gtosd::card_abstraction;
using Clock = std::chrono::steady_clock;

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

std::uint64_t parse_unsigned(const std::string_view value) {
  std::size_t consumed = 0U;
  const auto parsed = std::stoull(std::string{value}, &consumed, 10);
  if (consumed != value.size()) {
    throw std::runtime_error("invalid number: " + std::string{value});
  }
  return parsed;
}

// Compares ordinal order with the exact evaluator on random seven-card sets.
std::uint64_t verify_against_oracle(const ca::RankTable &ranks, const std::uint64_t samples) {
  ca::DeterministicRandom random(0x5645'5249'4659'0001ULL);
  std::array<std::uint8_t, 36> deck{};
  const auto draw = [&]() {
    for (std::uint8_t index = 0U; index < deck.size(); ++index) {
      deck[index] = index;
    }
    for (std::size_t drawn = 0U; drawn < 7U; ++drawn) {
      const auto offset = random.uniform_below(static_cast<std::uint32_t>(deck.size() - drawn));
      std::swap(deck[drawn], deck[drawn + offset]);
    }
    std::array<std::uint8_t, 7> result{};
    std::copy_n(deck.begin(), 7U, result.begin());
    std::sort(result.begin(), result.end());
    return result;
  };
  const auto cards_of = [](const std::array<std::uint8_t, 7> &values) {
    std::array<gtosd::CardId, 7> cards{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
      cards[index] = gtosd::CardId::from_index(values[index]).value();
    }
    return cards;
  };
  auto previous = draw();
  std::uint64_t mismatches = 0U;
  for (std::uint64_t sample = 0U; sample < samples; ++sample) {
    const auto current = draw();
    const auto oracle_previous = gtosd::evaluate_seven(cards_of(previous)).value();
    const auto oracle_current = gtosd::evaluate_seven(cards_of(current)).value();
    const auto rank_previous = ranks.rank_of_sorted(previous);
    const auto rank_current = ranks.rank_of_sorted(current);
    if ((rank_previous < rank_current) != (oracle_previous < oracle_current) ||
        (rank_previous == rank_current) != (oracle_previous == oracle_current)) {
      ++mismatches;
    }
    previous = current;
  }
  return mismatches;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path output_dir;
    unsigned threads = std::min(8U, std::max(1U, std::thread::hardware_concurrency()));
    std::uint64_t oracle_samples = 0U;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{name});
      }
      const std::string_view value = argv[++index];
      if (name == "--output-dir") {
        output_dir = value;
      } else if (name == "--threads") {
        threads = static_cast<unsigned>(parse_unsigned(value));
      } else if (name == "--verify-oracle") {
        oracle_samples = parse_unsigned(value);
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }

    const auto started = Clock::now();
    auto phase = Clock::now();
    const auto catalog = ca::BoardCatalog::build();
    const auto catalog_seconds = seconds_since(phase);

    phase = Clock::now();
    const auto ranks = ca::RankTable::build();
    if (!ranks) {
      throw std::runtime_error("rank table build failed");
    }
    const auto rank_seconds = seconds_since(phase);

    phase = Clock::now();
    const auto oracle_mismatches = verify_against_oracle(ranks.value(), oracle_samples);
    const auto oracle_seconds = seconds_since(phase);

    phase = Clock::now();
    const auto all_in = ca::AllInTable::build(ranks.value(), threads);
    if (!all_in) {
      throw std::runtime_error("all-in table build failed");
    }
    const auto all_in_seconds = seconds_since(phase);

    phase = Clock::now();
    const auto groups = ca::OpponentGroups::build(all_in.value());
    const auto groups_seconds = seconds_since(phase);

    phase = Clock::now();
    const auto flop = ca::FlopFeatureTable::build(catalog, ranks.value(), threads);
    if (!flop) {
      throw std::runtime_error("flop features build failed");
    }
    const auto flop_seconds = seconds_since(phase);

    phase = Clock::now();
    const auto turn = ca::TurnFeatureTable::build(catalog, ranks.value(), threads);
    if (!turn) {
      throw std::runtime_error("turn features build failed");
    }
    const auto turn_seconds = seconds_since(phase);

    phase = Clock::now();
    const auto river = ca::RiverFeatureTable::build(catalog, ranks.value(), groups, threads);
    if (!river) {
      throw std::runtime_error("river features build failed");
    }
    const auto river_seconds = seconds_since(phase);

    bool saved = false;
    bool verified = false;
    double save_seconds = 0.0;
    double reload_seconds = 0.0;
    if (!output_dir.empty()) {
      std::filesystem::create_directories(output_dir);
      phase = Clock::now();
      saved = ranks.value().save(output_dir / "rank_table_v1.bin").has_value() &&
              all_in.value().save(output_dir / "preflop_all_in_v1.bin").has_value() &&
              groups.save(output_dir / "opponent_groups_v1.bin").has_value() &&
              flop.value().save(output_dir / "flop_features_v1.bin").has_value() &&
              turn.value().save(output_dir / "turn_features_v1.bin").has_value() &&
              river.value().save(output_dir / "river_features_v1.bin").has_value() &&
              catalog.save(output_dir / "board_catalog_v1.bin").has_value();
      save_seconds = seconds_since(phase);
      phase = Clock::now();
      const auto loaded_ranks = ca::RankTable::load(output_dir / "rank_table_v1.bin");
      const auto loaded_all_in = ca::AllInTable::load(output_dir / "preflop_all_in_v1.bin");
      const auto loaded_groups = ca::OpponentGroups::load(output_dir / "opponent_groups_v1.bin");
      const auto loaded_flop = ca::FlopFeatureTable::load(output_dir / "flop_features_v1.bin");
      const auto loaded_turn = ca::TurnFeatureTable::load(output_dir / "turn_features_v1.bin");
      const auto loaded_river = ca::RiverFeatureTable::load(output_dir / "river_features_v1.bin");
      const auto loaded_catalog = ca::BoardCatalog::load(output_dir / "board_catalog_v1.bin");
      verified = saved && loaded_ranks && loaded_ranks.value() == ranks.value() && loaded_all_in &&
                 loaded_all_in.value() == all_in.value() && loaded_groups &&
                 loaded_groups.value().fingerprint == groups.fingerprint && loaded_flop &&
                 loaded_flop.value().fingerprint() == flop.value().fingerprint() && loaded_turn &&
                 loaded_turn.value().fingerprint() == turn.value().fingerprint() && loaded_river &&
                 loaded_river.value().fingerprint() == river.value().fingerprint() &&
                 loaded_catalog && loaded_catalog.value() == catalog;
      reload_seconds = seconds_since(phase);
    }

    std::cout << "{\n"
              << "  \"schema\": \"gtosd.preflop_blueprint_resources_report.v1\",\n"
              << "  \"threads\": " << threads << ",\n"
              << "  \"catalog_fingerprint\": \"" << catalog.fingerprint() << "\",\n"
              << "  \"rank_table\": {\"fingerprint\": \"" << ranks.value().fingerprint()
              << "\", \"distinct_ranks\": " << ranks.value().distinct_ranks()
              << ", \"payload_bytes\": " << ranks.value().payload_bytes()
              << ", \"build_seconds\": " << rank_seconds << ", \"oracle_samples\": " << oracle_samples
              << ", \"oracle_mismatches\": " << oracle_mismatches
              << ", \"oracle_seconds\": " << oracle_seconds << "},\n"
              << "  \"all_in_table\": {\"fingerprint\": \"" << all_in.value().fingerprint()
              << "\", \"payload_bytes\": " << all_in.value().payload_bytes()
              << ", \"build_seconds\": " << all_in_seconds << "},\n"
              << "  \"opponent_groups\": {\"fingerprint\": \"" << groups.fingerprint
              << "\", \"masses\": [";
    for (std::size_t group = 0U; group < groups.group_mass.size(); ++group) {
      std::cout << (group == 0U ? "" : ", ") << groups.group_mass[group];
    }
    std::cout << "], \"build_seconds\": " << groups_seconds << "},\n"
              << "  \"flop_features\": {\"fingerprint\": \"" << flop.value().fingerprint()
              << "\", \"payload_bytes\": " << flop.value().payload_bytes()
              << ", \"build_seconds\": " << flop_seconds << "},\n"
              << "  \"turn_features\": {\"fingerprint\": \"" << turn.value().fingerprint()
              << "\", \"payload_bytes\": " << turn.value().payload_bytes()
              << ", \"build_seconds\": " << turn_seconds << "},\n"
              << "  \"river_features\": {\"fingerprint\": \"" << river.value().fingerprint()
              << "\", \"payload_bytes\": " << river.value().payload_bytes()
              << ", \"build_seconds\": " << river_seconds << "},\n"
              << "  \"catalog_seconds\": " << catalog_seconds << ",\n"
              << "  \"total_seconds\": " << seconds_since(started) << ",\n"
              << "  \"output_dir\": \"" << output_dir.generic_string() << "\",\n"
              << "  \"saved\": " << (saved ? "true" : "false") << ",\n"
              << "  \"reload_verified\": " << (verified ? "true" : "false") << ",\n"
              << "  \"save_seconds\": " << save_seconds << ",\n"
              << "  \"reload_seconds\": " << reload_seconds << "\n"
              << "}\n";
    const bool ok = oracle_mismatches == 0U && (output_dir.empty() || verified);
    std::cout << "PREFLOP_BLUEPRINT_RESOURCES=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_RESOURCES=FAIL " << error.what() << '\n';
    return 1;
  }
}
