#include "gtosd/memory/memory.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void test_config_specific_preflight() {
  const auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1);
  require(config.has_value(), "PF-F1 config builds");

  const auto lazy =
      gtosd::analyze_postflop_config(config.value(), gtosd::MemoryPrototype::LazyInRam);
  const auto out_of_core =
      gtosd::analyze_postflop_config(config.value(), gtosd::MemoryPrototype::OutOfCore);
  require(lazy.has_value() && out_of_core.has_value(), "both production backends preflight");
  require(lazy.value().information_sets == 30'873'216U && lazy.value().actions == 66'756'096U,
          "PF-F1 preserves exact physical infosets and actions");
  require(lazy.value().memory.peak_resident_bytes == 5'622'269'688U,
          "lazy PF-F1 estimate remains stable");
  require(out_of_core.value().memory.backing_store_bytes == 4'554'172'152U,
          "out-of-core PF-F1 backing estimate remains stable");
  require(lazy.value().exact_outcomes && !lazy.value().uses_bucketing,
          "production preflight remains exact and unbucketed");
}

void test_invalid_config_is_rejected() {
  gtosd::PostflopTreeConfig invalid;
  const auto report = gtosd::analyze_postflop_config(invalid, gtosd::MemoryPrototype::LazyInRam);
  require(!report && report.error() == gtosd::MemoryError::InvalidConfiguration,
          "invalid production config returns a typed error");
}

} // namespace

int main() {
  try {
    test_config_specific_preflight();
    test_invalid_config_is_rejected();
    std::cout << "F7_POSTFLOP_PREFLIGHT_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F7_POSTFLOP_PREFLIGHT_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
