#include "gtosd/preflop/hu_preflop.hpp"

#include <iostream>

int main() {
  const auto config = gtosd::make_hu_co40_benchmark_config();
  const auto valid = gtosd::validate_hu_preflop_config(config);
  if (!valid) {
    std::cerr << "HU_PREFLOP_TRAINER_ISOLATION=FAIL config\n";
    return 1;
  }
  const auto tree = gtosd::build_hu_preflop_tree(config);
  if (!tree || tree.value().stats.node_count != 58U || tree.value().stats.decision_nodes != 20U ||
      tree.value().stats.postflop_entries != 9U) {
    std::cerr << "HU_PREFLOP_TRAINER_ISOLATION=FAIL tree\n";
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
  if (!solved ||
      !solved.value().algorithm_id.starts_with("external_sampling_v2_opponent_pass_average") ||
      !gtosd::validate_hu_preflop_blueprint(tree.value(), solved.value().preflop_blueprint)) {
    std::cerr << "HU_PREFLOP_TRAINER_ISOLATION=FAIL solve\n";
    return 3;
  }
  std::cout << "HU_PREFLOP_TRAINER_ISOLATION=PASS algorithm=" << solved.value().algorithm_id
            << " infosets=" << solved.value().information_sets << '\n';
  return 0;
}
