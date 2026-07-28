#include "gtosd/memory/memory.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef GTOSD_SOURCE_DIR
#define GTOSD_SOURCE_DIR "."
#endif

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void require_near(const double actual, const double expected, const double tolerance,
                  const std::string_view message) {
  ++assertions;
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(std::string(message));
  }
}

void test_benchmark_contracts_and_symbolic_counts() {
  const auto fixture_path = std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "fixtures" /
                            "postflop_memory_benchmarks.json";
  std::ifstream fixture(fixture_path, std::ios::binary);
  const std::string fixture_text((std::istreambuf_iterator<char>(fixture)),
                                 std::istreambuf_iterator<char>());
  require(static_cast<bool>(fixture) && fixture_text.find("\"PF-F1\"") != std::string::npos &&
              fixture_text.find("\"PF-F2\"") != std::string::npos &&
              fixture_text.find("\"PF-F3\"") != std::string::npos &&
              fixture_text.find("all_630_physical_combos") != std::string::npos,
          "versioned benchmark and full-range fixture is present");

  const auto pf_f1 = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1);
  const auto pf_f2 = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF2);
  const auto pf_f3 = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF3);
  require(pf_f1.has_value() && pf_f2.has_value() && pf_f3.has_value(),
          "all canonical postflop benchmarks build");
  require(pf_f1.value().effective_stack == gtosd::Money::from_antes(20).value(),
          "PF-F1 has 20 ante effective stack");
  require(pf_f2.value().effective_stack == gtosd::Money::from_antes(100).value() &&
              pf_f3.value().effective_stack == gtosd::Money::from_antes(100).value(),
          "PF-F2 and PF-F3 have 100 ante effective stack");
  require(pf_f1.value().streets[0].players[0][0].aggressive_sizes.size() == 1U &&
              pf_f2.value().streets[0].players[0][0].aggressive_sizes.size() == 2U &&
              pf_f3.value().streets[0].players[0][0].aggressive_sizes.size() == 3U,
          "benchmark size counts are 1, 2 and 3");
  require(pf_f1.value().streets[0].players[0][0].raise_depth == 1U &&
              pf_f2.value().streets[0].players[0][0].raise_depth == 2U &&
              pf_f3.value().streets[0].players[0][0].raise_depth == 4U,
          "benchmark raise depths are 1, 2 and 4");

  const auto report = gtosd::analyze_memory_prototype(gtosd::PostflopBenchmark::PfF1,
                                                      gtosd::MemoryPrototype::LazyInRam);
  require(report.has_value(), "PF-F1 symbolic tree and memory counts complete");
  require(report.value().public_tree.node_count > 0U &&
              report.value().public_tree.edge_count + 1U == report.value().public_tree.node_count,
          "physical public tree remains a rooted tree");
  require(report.value().information_sets_by_street[0] ==
                  report.value().public_tree.decision_nodes_by_street[0] * 528U &&
              report.value().information_sets_by_street[1] ==
                  report.value().public_tree.decision_nodes_by_street[1] * 496U &&
              report.value().information_sets_by_street[2] ==
                  report.value().public_tree.decision_nodes_by_street[2] * 465U,
          "exact private combo multiplicities are preserved by street");
  require(report.value().range_state_slots_by_street[0] ==
                  report.value().public_tree.node_count_by_street[0] * 528U &&
              report.value().range_state_slots_by_street[1] ==
                  report.value().public_tree.node_count_by_street[1] * 496U &&
              report.value().range_state_slots_by_street[2] ==
                  report.value().public_tree.node_count_by_street[2] * 465U,
          "reach and BR workspace cover every public state and legal private combo");
}

void test_all_prototype_reports() {
  constexpr std::uint64_t twelve_gib = 12ULL * 1'024ULL * 1'024ULL * 1'024ULL;
  for (std::uint8_t benchmark_index = 0; benchmark_index < 3U; ++benchmark_index) {
    for (std::uint8_t prototype_index = 0; prototype_index < 3U; ++prototype_index) {
      const auto report =
          gtosd::analyze_memory_prototype(static_cast<gtosd::PostflopBenchmark>(benchmark_index),
                                          static_cast<gtosd::MemoryPrototype>(prototype_index));
      require(report.has_value(), "each benchmark/prototype report succeeds");
      require(report.value().exact_outcomes && !report.value().uses_bucketing,
              "every prototype preserves exact outcomes without bucketing");
      require(report.value().memory.peak_resident_bytes > 0U &&
                  report.value().bytes_per_information_set > 0.0 &&
                  report.value().preflop_full_projection_bytes > 0U,
              "reports publish resident bytes, density and PRE-FULL projection");
      if (report.value().prototype == gtosd::MemoryPrototype::StreetDecomposition) {
        const auto expected_boundary =
            report.value().public_tree.chance_edges_by_street[0] * 496U * 2U * sizeof(double) +
            report.value().public_tree.chance_edges_by_street[1] * 465U * 2U * sizeof(double);
        require(report.value().memory.boundary_bytes == expected_boundary,
                "street decomposition preserves flop-turn and turn-river boundaries");
      }
    }
  }
  const auto out_of_core = gtosd::analyze_memory_prototype(gtosd::PostflopBenchmark::PfF1,
                                                           gtosd::MemoryPrototype::OutOfCore);
  require(out_of_core.has_value() && out_of_core.value().memory.peak_resident_bytes <= twelve_gib,
          "PF-F1 out-of-core prototype passes the 12 GiB resident gate");

  const auto custom_config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1);
  require(custom_config.has_value(), "custom preflight source config builds");
  const auto custom =
      gtosd::analyze_postflop_config(custom_config.value(), gtosd::MemoryPrototype::LazyInRam);
  const auto canonical = gtosd::analyze_memory_prototype(gtosd::PostflopBenchmark::PfF1,
                                                         gtosd::MemoryPrototype::LazyInRam);
  require(custom.has_value() && canonical.has_value() &&
              custom.value().information_sets == canonical.value().information_sets &&
              custom.value().actions == canonical.value().actions &&
              custom.value().memory.peak_resident_bytes ==
                  canonical.value().memory.peak_resident_bytes,
          "production config preflight matches the canonical PF-F1 report");
}

void test_checkpoint_parity() {
  const auto game = gtosd::make_leduc_poker_game();
  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::CfrPlus;
  config.iterations = 100;
  config.averaging_delay = 10;
  const auto solved = gtosd::solve_finite_game(game, config);
  require(solved.has_value(), "CFR+ source checkpoint builds");
  const auto source_bytes = gtosd::serialize_solver_checkpoint(solved.value().checkpoint);
  const auto source_metrics = gtosd::calculate_nash_conv(game, solved.value().average_strategy);
  require(source_bytes.has_value() && source_metrics.has_value(),
          "source checkpoint and metrics are valid");

  const auto backing_path = std::filesystem::current_path() / "gtosd_phase6_out_of_core_test.bin";
  for (std::uint8_t prototype_index = 0; prototype_index < 3U; ++prototype_index) {
    const auto prototype = static_cast<gtosd::MemoryPrototype>(prototype_index);
    const auto round_trip = gtosd::round_trip_checkpoint_memory(
        solved.value().checkpoint, prototype,
        prototype == gtosd::MemoryPrototype::OutOfCore ? backing_path.string() : std::string{});
    require(round_trip.has_value(), "each memory backend round-trips the checkpoint");
    const auto restored_bytes = gtosd::serialize_solver_checkpoint(round_trip.value().checkpoint);
    const auto restored_strategy = gtosd::average_strategy_profile(round_trip.value().checkpoint);
    require(restored_bytes.has_value() && restored_bytes.value() == source_bytes.value(),
            "round-trip is byte-identical");
    require(restored_strategy.has_value(), "restored average strategy is available");
    const auto restored_metrics = gtosd::calculate_nash_conv(game, restored_strategy.value());
    require(restored_metrics.has_value(), "restored strategy certifies");
    require_near(restored_metrics.value().profile_value[0], source_metrics.value().profile_value[0],
                 0.0, "EV parity is exact");
    require_near(restored_metrics.value().nash_conv, source_metrics.value().nash_conv, 0.0,
                 "NashConv parity is exact");
    if (prototype == gtosd::MemoryPrototype::OutOfCore) {
      require(round_trip.value().page_reads > 0U && round_trip.value().page_writes > 0U,
              "out-of-core backend reports physical page traffic");
    }
  }
  std::error_code remove_error;
  std::filesystem::remove(backing_path, remove_error);
  require(!remove_error, "test backing file is removed");
}

void test_bounded_out_of_core_residency() {
  auto report = gtosd::analyze_memory_prototype(gtosd::PostflopBenchmark::PfF1,
                                                gtosd::MemoryPrototype::OutOfCore);
  require(report.has_value(), "out-of-core report builds for residency probe");
  report.value().memory.backing_store_bytes = 16U * 1'024U * 1'024U;
  gtosd::MemoryPrototypeOptions options;
  options.page_size_bytes = 4'096U;
  options.resident_page_count = 16U;
  const auto backing_path = std::filesystem::current_path() / "gtosd_phase6_residency_probe.bin";
  std::error_code stale_remove_error;
  std::filesystem::remove(backing_path, stale_remove_error);
  const auto probe =
      gtosd::probe_out_of_core_residency(report.value(), backing_path.string(), options);
  require(probe.has_value(), "out-of-core residency probe succeeds");
  require(probe.value().logical_backing_bytes == 16U * 1'024U * 1'024U &&
              probe.value().touched_bytes == 32U * 4'096U &&
              probe.value().page_reads > options.resident_page_count &&
              probe.value().page_writes >= options.resident_page_count &&
              probe.value().measured_peak_rss_bytes > 0U,
          "probe reports logical capacity, LRU eviction traffic and measured RSS");
  std::error_code remove_error;
  std::filesystem::remove(backing_path, remove_error);
  require(!remove_error, "residency probe file is removed");
}

void test_errors_are_explicit() {
  gtosd::MemoryPrototypeOptions invalid_options;
  invalid_options.page_size_bytes = 5'000U;
  const auto invalid = gtosd::analyze_memory_prototype(
      gtosd::PostflopBenchmark::PfF1, gtosd::MemoryPrototype::OutOfCore, invalid_options);
  require(!invalid && invalid.error() == gtosd::MemoryError::InvalidConfiguration,
          "invalid paging configuration returns a typed error");

  gtosd::SolverCheckpoint empty;
  const auto missing_path =
      gtosd::round_trip_checkpoint_memory(empty, gtosd::MemoryPrototype::OutOfCore);
  require(!missing_path && missing_path.error() == gtosd::MemoryError::InvalidConfiguration,
          "out-of-core backend requires an explicit backing path");
}

} // namespace

int main() {
  try {
    test_benchmark_contracts_and_symbolic_counts();
    test_all_prototype_reports();
    test_checkpoint_parity();
    test_bounded_out_of_core_residency();
    test_errors_are_explicit();
    std::cout << "F6_MEMORY_PROTOTYPE_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "benchmarks=3\n"
              << "prototypes=3\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F6_MEMORY_PROTOTYPE_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "F6_MEMORY_PROTOTYPE_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
