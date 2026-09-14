#include "gtosd/preflop/hu_preflop.hpp"

#include <iostream>

int main() {
  const auto config = gtosd::make_hu_co40_benchmark_config();
  const auto valid = gtosd::validate_hu_preflop_config(config);
  if (!valid) {
    std::cerr << "HU_PREFLOP_TRAINER=FAIL reason=config error="
              << gtosd::hu_preflop_error_name(valid.error()) << '\n';
    return 1;
  }
  const auto tree = gtosd::build_hu_preflop_tree(config);
  if (!tree) {
    std::cerr << "HU_PREFLOP_TRAINER=FAIL reason=tree error="
              << gtosd::hu_preflop_error_name(tree.error()) << '\n';
    return 2;
  }
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 2U;
  options.evaluation_deals = 2U;
  options.best_response_iterations = 1U;
  options.best_response_evaluation_deals = 2U;
  options.equity_samples_per_bucket = 0U;
  options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ExternalSampling;
  options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
  const auto solved = gtosd::solve_hu_preflop_sampled(tree.value(), options);
  if (!solved) {
    std::cerr << "HU_PREFLOP_TRAINER=FAIL reason=solve error="
              << gtosd::hu_preflop_error_name(solved.error()) << '\n';
    return 3;
  }
  std::cout << "HU_PREFLOP_TRAINER=PASS"
            << " fingerprint=" << tree.value().fingerprint
            << " preflop_nodes=" << tree.value().stats.node_count
            << " preflop_decisions=" << tree.value().stats.decision_nodes
            << " postflop_entries=" << tree.value().stats.postflop_entries
            << " algorithm=" << solved.value().algorithm_id
            << " infosets=" << solved.value().information_sets << '\n';
  return 0;
}
