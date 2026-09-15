// Builds the canonical board catalogs, reports counts, multiplicity sums,
// construction times and the catalog fingerprint. Optionally saves the
// catalog and verifies a saved file against a fresh build.

#include "gtosd/card_abstraction/canonical_boards.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace ca = gtosd::card_abstraction;

std::uint64_t multiplicity_sum(const auto &entries) {
  std::uint64_t sum = 0U;
  for (const auto &entry : entries) {
    sum += entry.multiplicity;
  }
  return sum;
}

} // namespace

int main(const int argc, char **argv) {
  std::filesystem::path output;
  std::filesystem::path verify;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name = argv[index];
    if ((name == "--output" || name == "--verify") && index + 1 < argc) {
      (name == "--output" ? output : verify) = argv[++index];
    } else {
      std::cerr << "usage: preflop_blueprint_catalog [--output <file>] [--verify <file>]\n";
      return 2;
    }
  }

  ca::CatalogBuildTelemetry telemetry;
  const auto catalog = ca::BoardCatalog::build(&telemetry);
  const auto build_seconds = telemetry.flop_seconds + telemetry.flop_turn_seconds +
                             telemetry.river_board_seconds + telemetry.history_seconds +
                             telemetry.cross_reference_seconds;

  const bool counts_ok = catalog.flops().size() == ca::canonical_flop_count &&
                         catalog.flop_turns().size() == ca::canonical_flop_turn_count &&
                         catalog.river_boards().size() == ca::canonical_river_board_count &&
                         catalog.histories().size() == ca::canonical_board_history_count &&
                         multiplicity_sum(catalog.flops()) == ca::physical_flops &&
                         multiplicity_sum(catalog.flop_turns()) == ca::physical_flop_turns &&
                         multiplicity_sum(catalog.river_boards()) == ca::physical_river_boards &&
                         multiplicity_sum(catalog.histories()) == ca::physical_board_histories;

  std::cout << "{\n"
            << "  \"schema\": \"gtosd.preflop_blueprint_catalog_report.v1\",\n"
            << "  \"fingerprint\": \"" << catalog.fingerprint() << "\",\n"
            << "  \"canonical_flops\": " << catalog.flops().size() << ",\n"
            << "  \"canonical_flop_turns\": " << catalog.flop_turns().size() << ",\n"
            << "  \"canonical_river_boards\": " << catalog.river_boards().size() << ",\n"
            << "  \"canonical_board_histories\": " << catalog.histories().size() << ",\n"
            << "  \"physical_flops\": " << multiplicity_sum(catalog.flops()) << ",\n"
            << "  \"physical_flop_turns\": " << multiplicity_sum(catalog.flop_turns()) << ",\n"
            << "  \"physical_river_boards\": " << multiplicity_sum(catalog.river_boards()) << ",\n"
            << "  \"physical_board_histories\": " << multiplicity_sum(catalog.histories()) << ",\n"
            << "  \"build_seconds\": {\"flop\": " << telemetry.flop_seconds
            << ", \"flop_turn\": " << telemetry.flop_turn_seconds
            << ", \"river_board\": " << telemetry.river_board_seconds
            << ", \"history\": " << telemetry.history_seconds
            << ", \"cross_reference\": " << telemetry.cross_reference_seconds
            << ", \"total\": " << build_seconds << "},\n"
            << "  \"catalog_bytes\": " << catalog.byte_size() << ",\n"
            << "  \"counts_match_expected\": " << (counts_ok ? "true" : "false") << "\n";

  int status = counts_ok ? 0 : 1;
  if (!output.empty()) {
    const auto saved = catalog.save(output);
    std::cout << ",  \"saved\": " << (saved ? "true" : "false") << "\n";
    if (!saved) {
      status = 1;
    }
  }
  if (!verify.empty()) {
    const auto loaded = ca::BoardCatalog::load(verify);
    const bool same = loaded.has_value() && loaded.value() == catalog;
    std::cout << ",  \"verified\": " << (same ? "true" : "false") << "\n";
    if (!same) {
      status = 1;
    }
  }
  std::cout << "}\n";
  std::cout << "PREFLOP_BLUEPRINT_CATALOG=" << (status == 0 ? "PASS" : "FAIL") << '\n';
  return status;
}
