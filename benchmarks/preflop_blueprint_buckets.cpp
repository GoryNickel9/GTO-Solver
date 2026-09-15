// Clusters the exact street features into bucket tables and reports the
// clustering diagnostics. Resources are loaded from --resources-dir (written
// by gtosd_preflop_blueprint_resources) or built in-process when absent.

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
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

ca::OpponentGroups synthetic_groups() {
  std::array<std::uint8_t, 81> ranking{};
  std::array<double, 81> equity{};
  for (std::uint8_t index = 0U; index < ranking.size(); ++index) {
    ranking[index] = index;
    equity[index] = 1.0 - static_cast<double>(index) / 81.0;
  }
  return ca::OpponentGroups::from_ranking(ranking, equity, "synthetic_smoke_groups");
}

void print_diagnostics(const std::string_view street, const ca::BucketTable &table,
                       const ca::ClusteringDiagnostics &diagnostics, const bool last) {
  const auto occupancy_min =
      *std::min_element(diagnostics.occupancy_rows.begin(), diagnostics.occupancy_rows.end());
  const auto occupancy_max =
      *std::max_element(diagnostics.occupancy_rows.begin(), diagnostics.occupancy_rows.end());
  double mean_distance = 0.0;
  for (std::size_t bucket = 0U; bucket < diagnostics.mean_distance.size(); ++bucket) {
    mean_distance += diagnostics.mean_distance[bucket] *
                     static_cast<double>(diagnostics.occupancy_weight[bucket]);
  }
  mean_distance /= static_cast<double>(std::max<std::uint64_t>(1U, diagnostics.total_weight));
  std::cout << "  \"" << street << "\": {\"fingerprint\": \"" << table.fingerprint()
            << "\", \"capacity\": " << table.capacity() << ", \"rows\": " << table.rows()
            << ", \"observations\": " << diagnostics.observations
            << ", \"total_weight\": " << diagnostics.total_weight
            << ", \"chosen_restart\": " << diagnostics.chosen_restart
            << ", \"iterations\": " << diagnostics.iterations
            << ", \"final_inertia\": " << diagnostics.inertia_by_iteration.back()
            << ", \"mean_distance\": " << mean_distance
            << ", \"occupancy_rows_min\": " << occupancy_min
            << ", \"occupancy_rows_max\": " << occupancy_max
            << ", \"empty_reseeds\": " << diagnostics.empty_reseeds
            << ", \"payload_bytes\": " << table.payload_bytes()
            << ", \"seconds\": " << diagnostics.seconds << "}" << (last ? "\n" : ",\n");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path output_dir;
    unsigned threads = std::min(8U, std::max(1U, std::thread::hardware_concurrency()));
    std::uint16_t flop_capacity = 200U;
    std::uint16_t turn_capacity = 500U;
    std::uint16_t river_capacity = 1'000U;
    std::uint32_t restarts = 10U;
    std::uint32_t screening_iterations = 10U;
    std::uint32_t maximum_iterations = 25U;
    std::uint32_t screening_sample = 500'000U;
    std::uint64_t partition_seed = 0x5041'5254'4954'494FULL;
    bool synthetic = false;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--synthetic-groups") {
        synthetic = true;
        continue;
      }
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{name});
      }
      const std::string_view value = argv[++index];
      if (name == "--resources-dir") {
        resources_dir = value;
      } else if (name == "--output-dir") {
        output_dir = value;
      } else if (name == "--threads") {
        threads = static_cast<unsigned>(parse_unsigned(value));
      } else if (name == "--flop") {
        flop_capacity = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--turn") {
        turn_capacity = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--river") {
        river_capacity = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--restarts") {
        restarts = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--screening-iterations") {
        screening_iterations = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--max-iterations") {
        maximum_iterations = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--screening-sample") {
        screening_sample = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--partition-seed") {
        partition_seed = parse_unsigned(value);
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }

    const auto started = Clock::now();
    auto phase = Clock::now();
    const auto catalog = ca::BoardCatalog::build();
    std::optional<ca::RankTable> ranks;
    std::optional<ca::OpponentGroups> groups;
    std::optional<ca::FlopFeatureTable> flop_features;
    std::optional<ca::TurnFeatureTable> turn_features;
    std::optional<ca::RiverFeatureTable> river_features;
    bool loaded_resources = false;
    if (!resources_dir.empty()) {
      auto loaded_flop = ca::FlopFeatureTable::load(resources_dir / "flop_features_v1.bin");
      auto loaded_turn = ca::TurnFeatureTable::load(resources_dir / "turn_features_v1.bin");
      auto loaded_river = ca::RiverFeatureTable::load(resources_dir / "river_features_v1.bin");
      if (loaded_flop && loaded_turn && loaded_river) {
        flop_features.emplace(std::move(loaded_flop.value()));
        turn_features.emplace(std::move(loaded_turn.value()));
        river_features.emplace(std::move(loaded_river.value()));
        loaded_resources = true;
      }
    }
    if (!loaded_resources) {
      auto built_ranks = ca::RankTable::build();
      if (!built_ranks) {
        throw std::runtime_error("rank table build failed");
      }
      ranks.emplace(std::move(built_ranks.value()));
      if (synthetic) {
        groups.emplace(synthetic_groups());
      } else {
        const auto all_in = ca::AllInTable::build(ranks.value(), threads);
        if (!all_in) {
          throw std::runtime_error("all-in table build failed");
        }
        groups.emplace(ca::OpponentGroups::build(all_in.value()));
      }
      auto built_flop = ca::FlopFeatureTable::build(catalog, ranks.value(), threads);
      auto built_turn = ca::TurnFeatureTable::build(catalog, ranks.value(), threads);
      auto built_river = ca::RiverFeatureTable::build(catalog, ranks.value(), groups.value(), threads);
      if (!built_flop || !built_turn || !built_river) {
        throw std::runtime_error("feature build failed");
      }
      flop_features.emplace(std::move(built_flop.value()));
      turn_features.emplace(std::move(built_turn.value()));
      river_features.emplace(std::move(built_river.value()));
    }
    const auto preparation_seconds = seconds_since(phase);

    const auto make_parameters = [&](const std::uint16_t capacity) {
      ca::ClusteringParameters parameters;
      parameters.capacity = capacity;
      parameters.restarts = restarts;
      parameters.screening_iterations = screening_iterations;
      parameters.maximum_iterations = maximum_iterations;
      parameters.screening_sample = screening_sample;
      parameters.partition_seed = partition_seed;
      parameters.threads = threads;
      return parameters;
    };
    ca::ClusteringDiagnostics flop_diagnostics;
    ca::ClusteringDiagnostics turn_diagnostics;
    ca::ClusteringDiagnostics river_diagnostics;
    const auto flop = ca::BucketTable::build_flop(catalog, flop_features.value(),
                                                  make_parameters(flop_capacity), &flop_diagnostics);
    const auto turn = ca::BucketTable::build_turn(catalog, turn_features.value(),
                                                  make_parameters(turn_capacity), &turn_diagnostics);
    const auto river = ca::BucketTable::build_river(
        catalog, river_features.value(), make_parameters(river_capacity), &river_diagnostics);
    if (!flop || !turn || !river) {
      throw std::runtime_error("bucket clustering failed");
    }

    bool saved = false;
    bool verified = false;
    if (!output_dir.empty()) {
      std::filesystem::create_directories(output_dir);
      saved = flop.value().save(output_dir / "flop_buckets_v1.bin").has_value() &&
              turn.value().save(output_dir / "turn_buckets_v1.bin").has_value() &&
              river.value().save(output_dir / "river_buckets_v1.bin").has_value();
      const auto loaded_flop = ca::BucketTable::load(output_dir / "flop_buckets_v1.bin");
      const auto loaded_turn = ca::BucketTable::load(output_dir / "turn_buckets_v1.bin");
      const auto loaded_river = ca::BucketTable::load(output_dir / "river_buckets_v1.bin");
      verified = saved && loaded_flop && loaded_flop.value() == flop.value() && loaded_turn &&
                 loaded_turn.value() == turn.value() && loaded_river &&
                 loaded_river.value() == river.value();
    }

    std::cout << "{\n"
              << "  \"schema\": \"gtosd.preflop_blueprint_buckets_report.v1\",\n"
              << "  \"threads\": " << threads << ",\n"
              << "  \"resources\": \"" << (loaded_resources ? "loaded" : "built") << "\",\n"
              << "  \"synthetic_groups\": " << (synthetic && !loaded_resources ? "true" : "false")
              << ",\n"
              << "  \"catalog_fingerprint\": \"" << catalog.fingerprint() << "\",\n"
              << "  \"partition_seed\": " << partition_seed << ",\n"
              << "  \"restarts\": " << restarts << ", \"screening_iterations\": "
              << screening_iterations << ", \"maximum_iterations\": " << maximum_iterations
              << ", \"screening_sample\": " << screening_sample << ",\n"
              << "  \"preparation_seconds\": " << preparation_seconds << ",\n";
    print_diagnostics("flop", flop.value(), flop_diagnostics, false);
    print_diagnostics("turn", turn.value(), turn_diagnostics, false);
    print_diagnostics("river", river.value(), river_diagnostics, false);
    std::cout << "  \"output_dir\": \"" << output_dir.generic_string() << "\",\n"
              << "  \"saved\": " << (saved ? "true" : "false") << ",\n"
              << "  \"reload_verified\": " << (verified ? "true" : "false") << ",\n"
              << "  \"total_seconds\": " << seconds_since(started) << "\n}\n";
    const bool ok = output_dir.empty() || verified;
    std::cout << "PREFLOP_BLUEPRINT_BUCKETS=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_BUCKETS=FAIL " << error.what() << '\n';
    return 1;
  }
}
