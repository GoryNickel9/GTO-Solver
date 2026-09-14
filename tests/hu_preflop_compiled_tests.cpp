#include "gtosd/preflop/hu_preflop.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::HuPreflopSolveOptions exact_options() {
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 32U;
  options.evaluation_deals = 64U;
  options.best_response_iterations = 8U;
  options.best_response_evaluation_deals = 64U;
  options.equity_samples_per_bucket = 0U;
  options.seed = 0x5234'434F'4D50'0001ULL;
  options.partition_seed = 0x5234'5041'5254'0001ULL;
  options.evaluation_seed = 0x5234'4556'414C'0001ULL;
  options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ExternalSampling;
  options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
  return options;
}

void test_compiled_reference_equivalence(const gtosd::HuPreflopTree &tree) {
  auto compiled_options = exact_options();
  auto reference_options = compiled_options;
  reference_options.use_compiled_betting = false;
  const auto compiled = gtosd::solve_hu_preflop_sampled(tree, compiled_options);
  const auto reference = gtosd::solve_hu_preflop_sampled(tree, reference_options);
  require(compiled.has_value() && reference.has_value(),
          "compiled and reference betting traversals complete");
  require(compiled.value().root_strategy == reference.value().root_strategy,
          "compiled betting preserves the exact root strategy");
  require(
      compiled.value().root_ev_ante == reference.value().root_ev_ante &&
          compiled.value().root_ev_standard_error_ante ==
              reference.value().root_ev_standard_error_ante &&
          compiled.value().root_action_ev_ante == reference.value().root_action_ev_ante &&
          compiled.value().root_action_ev_standard_error_ante ==
              reference.value().root_action_ev_standard_error_ante &&
          compiled.value().root_action_ev_samples == reference.value().root_action_ev_samples &&
          compiled.value().best_response_co_ev_ante == reference.value().best_response_co_ev_ante &&
          compiled.value().best_response_btn_ev_ante == reference.value().best_response_btn_ev_ante,
      "compiled betting preserves seeded evaluation and response values");
  require(compiled.value().information_sets == reference.value().information_sets &&
              compiled.value().best_response_information_sets ==
                  reference.value().best_response_information_sets,
          "compiled betting preserves information-set cardinality");
  require(compiled.value().compiled_betting_nodes > 0U &&
              compiled.value().compiled_betting_bytes > 0U &&
              reference.value().compiled_betting_nodes == 0U &&
              reference.value().compiled_betting_bytes == 0U,
          "compiled betting reports its bounded static payload");
}

void test_explicit_numeric_memory_failure(const gtosd::HuPreflopTree &tree) {
  auto options = exact_options();
  options.maximum_numeric_state_bytes = 1'024U;
  const auto solve = gtosd::solve_hu_preflop_sampled(tree, options);
  require(!solve && solve.error() == gtosd::HuPreflopError::MemoryFailure,
          "numeric state exhaustion returns memory_failure without fallback");
}

void test_bounded_bucket_cache(const gtosd::HuPreflopTree &tree) {
  auto options = exact_options();
  options.iterations = 64U;
  options.equity_samples_per_bucket = 2U;
  options.maximum_bucket_cache_entries = 6U;
  options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype;
  const auto solve = gtosd::solve_hu_preflop_sampled(tree, options);
  require(solve.has_value(), "distributional solve works with the minimum cache budget");
  require(solve.value().bucket_cache_peak_entries <= options.maximum_bucket_cache_entries,
          "bucket cache never exceeds its declared entry budget");
  require(solve.value().bucket_cache_evictions > 0U,
          "minimum bucket cache exercises deterministic eviction");

  options.maximum_bucket_cache_entries = 5U;
  const auto invalid = gtosd::solve_hu_preflop_sampled(tree, options);
  require(!invalid && invalid.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "a cache budget that cannot cover six partitions is rejected");
}

} // namespace

int main() {
  try {
    const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
    require(tree.has_value(), "CO40 tree builds for R4 tests");
    test_compiled_reference_equivalence(tree.value());
    test_explicit_numeric_memory_failure(tree.value());
    test_bounded_bucket_cache(tree.value());
    std::cout << "R4_HU_PREFLOP_COMPILED_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "R4_HU_PREFLOP_COMPILED_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "R4_HU_PREFLOP_COMPILED_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
