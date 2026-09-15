#include "gtosd/core/ranges.hpp"
#include "gtosd/preflop/hu_preflop_abstract_game.hpp"
#include "gtosd/solver/tre_validation.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr double family_wise_alpha = 0.05;
constexpr std::uint32_t maximum_sequential_looks = 16U;
constexpr std::uint32_t simultaneous_metric_count = 5U;

struct Arguments {
  std::string config;
  std::string output;
  std::uint32_t training_deals_per_co_class{1U};
  std::uint32_t training_maximum_deals_per_co_class{8U};
  std::uint32_t response_deals_per_co_class{1U};
  std::uint32_t response_maximum_deals_per_co_class{8U};
  std::uint32_t evaluation_initial_deals_per_co_class{2U};
  std::uint32_t evaluation_maximum_deals_per_co_class{8U};
  std::uint32_t equity_samples{4U};
  std::uint64_t iterations{256U};
  std::uint64_t solver_seed{0x5452'4553'4F4C'5601ULL};
  std::uint64_t training_seed{0x5452'4554'5241'4901ULL};
  std::uint64_t response_seed{0x5452'4552'4553'5001ULL};
  std::uint64_t evaluation_seed{0x5452'4545'5641'4C01ULL};
  std::uint64_t partition_seed{0x5632'3350'4152'5401ULL};
  std::uint64_t maximum_nodes{2'000'000U};
  double evaluation_normalized_half_width_target{0.01};
  double minimum_policy_reach_coverage{0.90};
  double minimum_response_reach_coverage{0.80};
  double maximum_normalized_candidate_gain{0.01};
};

struct TrainingRun {
  Json game;
  gtosd::StrategyProfile policy;
  std::uint64_t traversed_nodes{0U};
  double maximum_normalization_error{0.0};
};

struct ResponseRun {
  Json game;
  Json players;
  Json training_policy_coverage;
  std::array<gtosd::StrategyProfile, 2> policies;
  double exact_nashconv{0.0};
};

std::uint64_t parse_unsigned(const std::string_view value, const std::string_view name) {
  std::size_t consumed = 0U;
  const auto parsed = std::stoull(std::string{value}, &consumed, 0);
  if (consumed != value.size() || parsed == 0U) {
    throw std::runtime_error("invalid " + std::string{name});
  }
  return parsed;
}

std::uint32_t parse_uint32(const std::string_view value, const std::string_view name) {
  const auto parsed = parse_unsigned(value, name);
  if (parsed > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(std::string{name} + " exceeds uint32");
  }
  return static_cast<std::uint32_t>(parsed);
}

double parse_positive_double(const std::string_view value, const std::string_view name) {
  std::size_t consumed = 0U;
  const double parsed = std::stod(std::string{value}, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed) || parsed <= 0.0) {
    throw std::runtime_error("invalid " + std::string{name});
  }
  return parsed;
}

std::uint32_t planned_looks(const std::uint32_t initial, const std::uint32_t maximum) {
  std::uint32_t looks = 1U;
  std::uint32_t current = initial;
  while (current < maximum) {
    current = current > maximum / 2U ? maximum : current * 2U;
    ++looks;
  }
  return looks;
}

Arguments parse_arguments(const int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name = argv[index];
    if (index + 1 >= argc) {
      throw std::runtime_error("missing argument value");
    }
    const std::string_view value = argv[++index];
    if (name == "--config") {
      arguments.config = value;
    } else if (name == "--output") {
      arguments.output = value;
    } else if (name == "--training-deals-per-co-class") {
      arguments.training_deals_per_co_class = parse_uint32(value, name);
    } else if (name == "--training-maximum-deals-per-co-class") {
      arguments.training_maximum_deals_per_co_class = parse_uint32(value, name);
    } else if (name == "--response-deals-per-co-class") {
      arguments.response_deals_per_co_class = parse_uint32(value, name);
    } else if (name == "--response-maximum-deals-per-co-class") {
      arguments.response_maximum_deals_per_co_class = parse_uint32(value, name);
    } else if (name == "--evaluation-initial-deals-per-co-class") {
      arguments.evaluation_initial_deals_per_co_class = parse_uint32(value, name);
    } else if (name == "--evaluation-maximum-deals-per-co-class") {
      arguments.evaluation_maximum_deals_per_co_class = parse_uint32(value, name);
    } else if (name == "--evaluation-normalized-half-width-target") {
      arguments.evaluation_normalized_half_width_target = parse_positive_double(value, name);
    } else if (name == "--minimum-policy-reach-coverage") {
      arguments.minimum_policy_reach_coverage = parse_positive_double(value, name);
    } else if (name == "--minimum-response-reach-coverage") {
      arguments.minimum_response_reach_coverage = parse_positive_double(value, name);
    } else if (name == "--maximum-normalized-candidate-gain") {
      arguments.maximum_normalized_candidate_gain = parse_positive_double(value, name);
    } else if (name == "--equity-samples") {
      arguments.equity_samples = parse_uint32(value, name);
    } else if (name == "--iterations") {
      arguments.iterations = parse_unsigned(value, name);
    } else if (name == "--solver-seed") {
      arguments.solver_seed = parse_unsigned(value, name);
    } else if (name == "--training-seed") {
      arguments.training_seed = parse_unsigned(value, name);
    } else if (name == "--response-seed") {
      arguments.response_seed = parse_unsigned(value, name);
    } else if (name == "--evaluation-seed") {
      arguments.evaluation_seed = parse_unsigned(value, name);
    } else if (name == "--partition-seed") {
      arguments.partition_seed = parse_unsigned(value, name);
    } else if (name == "--maximum-nodes") {
      arguments.maximum_nodes = parse_unsigned(value, name);
    } else {
      throw std::runtime_error("unknown argument " + std::string{name});
    }
  }

  const std::array corpus_seeds{arguments.training_seed, arguments.response_seed,
                                arguments.evaluation_seed};
  if (arguments.config.empty() || arguments.output.empty()) {
    throw std::runtime_error("--config and --output are required");
  }
  if (arguments.evaluation_initial_deals_per_co_class < 2U ||
      arguments.evaluation_initial_deals_per_co_class >
          arguments.evaluation_maximum_deals_per_co_class) {
    throw std::runtime_error("evaluation K must satisfy 2 <= initial <= maximum");
  }
  if (arguments.training_deals_per_co_class > arguments.training_maximum_deals_per_co_class ||
      arguments.response_deals_per_co_class > arguments.response_maximum_deals_per_co_class ||
      arguments.minimum_policy_reach_coverage > 1.0 ||
      arguments.minimum_response_reach_coverage > 1.0) {
    throw std::runtime_error("invalid automatic T/R/E controller limits");
  }
  if (corpus_seeds[0] == corpus_seeds[1] || corpus_seeds[0] == corpus_seeds[2] ||
      corpus_seeds[1] == corpus_seeds[2]) {
    throw std::runtime_error("training, response and evaluation seeds must be distinct");
  }
  const auto controller_looks = planned_looks(arguments.training_deals_per_co_class,
                                              arguments.training_maximum_deals_per_co_class) +
                                planned_looks(arguments.response_deals_per_co_class,
                                              arguments.response_maximum_deals_per_co_class) +
                                planned_looks(arguments.evaluation_initial_deals_per_co_class,
                                              arguments.evaluation_maximum_deals_per_co_class) -
                                2U;
  if (controller_looks > maximum_sequential_looks) {
    throw std::runtime_error("evaluation schedule exceeds sixteen planned looks");
  }
  return arguments;
}

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

Json estimate_json(const gtosd::TreEstimate &estimate, const double initial_pot) {
  return {{"mean_ante", estimate.mean},
          {"standard_error_ante", estimate.standard_error},
          {"confidence_half_width_ante", estimate.confidence_half_width},
          {"confidence_interval_ante", {estimate.confidence_lower, estimate.confidence_upper}},
          {"normalized_mean", estimate.mean / initial_pot},
          {"normalized_confidence_half_width", estimate.confidence_half_width / initial_pot},
          {"normalized_confidence_interval",
           {estimate.confidence_lower / initial_pot, estimate.confidence_upper / initial_pot}}};
}

gtosd::TreStratifiedLayout hu_preflop_layout(const std::uint32_t deals_per_co_class) {
  gtosd::TreStratifiedLayout layout;
  layout.total_outcomes =
      static_cast<std::size_t>(gtosd::hu_preflop_hand_class_count) * deals_per_co_class;
  layout.strata.reserve(gtosd::hu_preflop_hand_class_count);
  for (gtosd::HandClassId hand_class_id = 0U; hand_class_id < gtosd::hu_preflop_hand_class_count;
       ++hand_class_id) {
    layout.strata.push_back({gtosd::class_name(hand_class_id),
                             static_cast<std::size_t>(hand_class_id) * deals_per_co_class,
                             deals_per_co_class,
                             static_cast<double>(gtosd::class_mass(hand_class_id)) / 630.0});
  }
  return layout;
}

Json coverage_json(const gtosd::PolicyCoverageAudit &coverage) {
  return {{"contract", gtosd::policy_completion_rule_name(coverage.completion_rule)},
          {"target_game_fingerprint", coverage.target_game_fingerprint},
          {"supplied_information_sets", coverage.supplied_information_sets},
          {"required_information_sets", coverage.required_information_sets},
          {"matched_information_sets", coverage.matched_information_sets},
          {"unseen_information_sets", coverage.unseen_information_sets},
          {"unused_supplied_information_sets", coverage.unused_supplied_information_sets},
          {"required_information_sets_by_player", coverage.required_information_sets_by_player},
          {"unseen_information_sets_by_player", coverage.unseen_information_sets_by_player},
          {"decision_nodes", coverage.decision_nodes},
          {"unseen_decision_nodes", coverage.unseen_decision_nodes},
          {"exact_key_coverage", coverage.exact_key_coverage},
          {"total_decision_reach_mass", coverage.total_decision_reach_mass},
          {"unseen_decision_reach_mass", coverage.unseen_decision_reach_mass},
          {"reach_weighted_coverage", coverage.reach_weighted_coverage},
          {"total_decision_reach_mass_by_player", coverage.total_decision_reach_mass_by_player},
          {"unseen_decision_reach_mass_by_player", coverage.unseen_decision_reach_mass_by_player},
          {"reach_weighted_coverage_by_player", coverage.reach_weighted_coverage_by_player}};
}

Json game_json(const gtosd::HuPreflopCompiledAbstractGame &compiled, const double compile_seconds) {
  return {{"abstract_game_definition_fingerprint", compiled.definition.fingerprint},
          {"chance_corpus_fingerprint", compiled.chance_corpus.fingerprint},
          {"finite_game_fingerprint", compiled.summary.fingerprint},
          {"seed", compiled.chance_corpus.seed},
          {"deals_per_co_class", compiled.chance_corpus.deals_per_co_class},
          {"outcomes", compiled.chance_corpus.outcomes.size()},
          {"nodes", compiled.summary.nodes},
          {"terminal_nodes", compiled.summary.terminal_nodes},
          {"chance_nodes", compiled.summary.chance_nodes},
          {"decision_nodes", compiled.summary.decision_nodes},
          {"information_sets", compiled.summary.information_sets},
          {"maximum_depth", compiled.summary.maximum_depth},
          {"compile_seconds", compile_seconds}};
}

gtosd::StrategyProfile
deterministic_response_profile(const gtosd::StrategyProfile &response_game_profile,
                               const gtosd::BestResponseResult &best_response) {
  gtosd::StrategyProfile response;
  for (const auto &[information_set, selected_action] : best_response.policy) {
    const auto source = response_game_profile.find(information_set);
    if (source == response_game_profile.end() || source->second.player != best_response.player) {
      throw std::runtime_error("best-response information set is absent from response game");
    }
    auto strategy = source->second;
    std::ranges::fill(strategy.probabilities, 0.0);
    const auto selected = std::ranges::find(strategy.actions, selected_action);
    if (selected == strategy.actions.end()) {
      throw std::runtime_error("best-response action is absent from response game");
    }
    const auto index = static_cast<std::size_t>(selected - strategy.actions.begin());
    strategy.probabilities[index] = 1.0;
    response.emplace(information_set, std::move(strategy));
  }
  return response;
}

gtosd::HuPreflopSolveOptions v23_options(const Arguments &arguments) {
  gtosd::HuPreflopSolveOptions options;
  options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptivePerfectRecallV23;
  options.equity_samples_per_bucket = arguments.equity_samples;
  options.partition_seed = arguments.partition_seed;
  options.distributional_bucket_capacities = {32U, 128U, 512U};
  return options;
}

void write_result(const std::string &path, const std::string &payload) {
  const std::filesystem::path destination(path);
  auto temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("cannot open output temporary file");
    }
    output << payload << '\n';
    output.flush();
    if (!output) {
      throw std::runtime_error("cannot write output temporary file");
    }
  }
  std::error_code error;
  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    throw std::runtime_error("cannot atomically replace output file");
  }
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto arguments = parse_arguments(argc, argv);
    const auto total_started = Clock::now();
    std::ifstream config_input(arguments.config, std::ios::binary);
    if (!config_input) {
      throw std::runtime_error("cannot open config");
    }
    const std::string serialized_config{std::istreambuf_iterator<char>(config_input),
                                        std::istreambuf_iterator<char>()};
    const auto config_document = Json::parse(serialized_config);
    const auto game_id = config_document.at("id").get<std::string>();
    const auto config = gtosd::deserialize_hu_preflop_config_json(serialized_config);
    const auto tree =
        config
            ? gtosd::build_hu_preflop_tree(config.value())
            : gtosd::Result<gtosd::HuPreflopTree, gtosd::HuPreflopError>::failure(config.error());
    if (!tree) {
      throw std::runtime_error("cannot build HU preflop tree");
    }
    const auto options = v23_options(arguments);

    double training_compile_seconds = 0.0;
    double training_solve_seconds = 0.0;
    Json training_game;
    gtosd::StrategyProfile training_policy;
    std::uint64_t traversed_nodes = 0U;
    double maximum_normalization_error = 0.0;
    {
      const auto started = Clock::now();
      auto training = gtosd::compile_hu_preflop_abstract_game(
          tree.value(), options, arguments.training_deals_per_co_class, arguments.training_seed,
          arguments.maximum_nodes);
      training_compile_seconds = seconds_since(started);
      if (!training) {
        throw std::runtime_error(std::string{"training game compile failed: "} +
                                 gtosd::hu_preflop_error_name(training.error()));
      }
      training_game = game_json(training.value(), training_compile_seconds);
      gtosd::SolverConfig solver;
      solver.algorithm = gtosd::SolverAlgorithm::LinearMccfr;
      solver.iterations = arguments.iterations;
      solver.seed = arguments.solver_seed;
      const auto solve_started = Clock::now();
      auto solved = gtosd::solve_finite_game(training.value().game, solver);
      training_solve_seconds = seconds_since(solve_started);
      if (!solved) {
        throw std::runtime_error(std::string{"training Linear MCCFR failed: "} +
                                 gtosd::solver_error_name(solved.error()));
      }
      traversed_nodes = solved.value().traversed_nodes;
      maximum_normalization_error = solved.value().maximum_normalization_error;
      training_policy = std::move(solved.value().average_strategy);
    }

    double response_compile_seconds = 0.0;
    double response_search_seconds = 0.0;
    Json response_game;
    Json response_search = Json::array();
    Json training_on_response_coverage;
    std::array<gtosd::StrategyProfile, 2> response_policies;
    double exact_response_corpus_nashconv = 0.0;
    {
      const auto started = Clock::now();
      auto response = gtosd::compile_hu_preflop_abstract_game(
          tree.value(), options, arguments.response_deals_per_co_class, arguments.response_seed,
          arguments.maximum_nodes);
      response_compile_seconds = seconds_since(started);
      if (!response) {
        throw std::runtime_error(std::string{"response game compile failed: "} +
                                 gtosd::hu_preflop_error_name(response.error()));
      }
      response_game = game_json(response.value(), response_compile_seconds);
      const auto completed = gtosd::complete_strategy_profile(
          response.value().game, training_policy, gtosd::PolicyCompletionRule::UniformUnseenV1);
      if (!completed) {
        throw std::runtime_error(std::string{"training policy completion on R failed: "} +
                                 gtosd::solver_error_name(completed.error()));
      }
      training_on_response_coverage = coverage_json(completed.value().coverage);
      const auto baseline =
          gtosd::evaluate_strategy_profile(response.value().game, completed.value().profile);
      if (!baseline) {
        throw std::runtime_error("training policy evaluation on R failed");
      }

      const auto search_started = Clock::now();
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        const auto best_response =
            gtosd::exact_best_response(response.value().game, completed.value().profile, player);
        if (!best_response) {
          throw std::runtime_error(std::string{"exact response search failed: "} +
                                   gtosd::solver_error_name(best_response.error()));
        }
        const double gain = best_response.value().value - baseline.value()[player];
        if (gain < -1.0e-12) {
          throw std::runtime_error("exact response underperforms baseline on R");
        }
        response_policies[player] =
            deterministic_response_profile(completed.value().profile, best_response.value());
        exact_response_corpus_nashconv += std::max(0.0, gain);
        response_search.push_back({{"player", player},
                                   {"baseline_ev_ante", baseline.value()[player]},
                                   {"exact_best_response_ev_ante", best_response.value().value},
                                   {"exact_response_gain_ante", std::max(0.0, gain)},
                                   {"response_information_sets", response_policies[player].size()},
                                   {"exact_on_response_corpus", true}});
      }
      response_search_seconds = seconds_since(search_started);
    }

    const std::uint32_t look_count = planned_looks(arguments.evaluation_initial_deals_per_co_class,
                                                   arguments.evaluation_maximum_deals_per_co_class);
    const auto confidence =
        gtosd::make_tre_confidence_plan(family_wise_alpha, simultaneous_metric_count, look_count);
    if (!confidence) {
      throw std::runtime_error("cannot build T/R/E confidence plan");
    }
    std::uint32_t evaluation_k = arguments.evaluation_initial_deals_per_co_class;
    bool precision_target_met = false;
    double evaluation_compile_seconds = 0.0;
    double evaluation_seconds = 0.0;
    Json evaluation_looks = Json::array();
    Json final_evaluation;

    while (true) {
      const auto compile_started = Clock::now();
      auto evaluation = gtosd::compile_hu_preflop_abstract_game(
          tree.value(), options, evaluation_k, arguments.evaluation_seed, arguments.maximum_nodes);
      const double compile_seconds = seconds_since(compile_started);
      evaluation_compile_seconds += compile_seconds;
      if (!evaluation) {
        throw std::runtime_error(std::string{"evaluation game compile failed: "} +
                                 gtosd::hu_preflop_error_name(evaluation.error()));
      }

      const auto evaluation_started = Clock::now();
      const auto paired = gtosd::evaluate_tre_paired_look(
          evaluation.value().game, training_policy, response_policies,
          hu_preflop_layout(evaluation_k), confidence.value(),
          gtosd::PolicyCompletionRule::UniformUnseenV1);
      if (!paired) {
        throw std::runtime_error(std::string{"generic paired evaluation failed: "} +
                                 gtosd::solver_error_name(paired.error()));
      }
      Json response_coverage = Json::array();
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        response_coverage.push_back(
            {{"player", player},
             {"coverage", coverage_json(paired.value().response_coverage[player])}});
      }
      const auto &profile_estimates = paired.value().profile_value;
      const auto &gain_estimates = paired.value().candidate_response_gain;
      const auto &gain_sum_estimate = paired.value().candidate_response_gain_sum;
      const double maximum_normalized_half_width =
          paired.value().maximum_normalized_confidence_half_width;
      precision_target_met =
          maximum_normalized_half_width <= arguments.evaluation_normalized_half_width_target;
      const double look_seconds = seconds_since(evaluation_started);
      evaluation_seconds += look_seconds;
      Json look{{"look", evaluation_looks.size() + 1U},
                {"game", game_json(evaluation.value(), compile_seconds)},
                {"training_policy_coverage", coverage_json(paired.value().baseline_coverage)},
                {"response_policy_coverage", std::move(response_coverage)},
                {"profile_ev",
                 {estimate_json(profile_estimates[0], evaluation.value().game.initial_pot),
                  estimate_json(profile_estimates[1], evaluation.value().game.initial_pot)}},
                {"candidate_response_gain",
                 {estimate_json(gain_estimates[0], evaluation.value().game.initial_pot),
                  estimate_json(gain_estimates[1], evaluation.value().game.initial_pot)}},
                {"candidate_response_gain_sum",
                 estimate_json(gain_sum_estimate, evaluation.value().game.initial_pot)},
                {"maximum_normalized_confidence_half_width", maximum_normalized_half_width},
                {"precision_target_met", precision_target_met},
                {"evaluation_seconds", look_seconds}};
      evaluation_looks.push_back(look);
      final_evaluation = std::move(look);
      if (precision_target_met || evaluation_k == arguments.evaluation_maximum_deals_per_co_class) {
        break;
      }
      evaluation_k = evaluation_k > arguments.evaluation_maximum_deals_per_co_class / 2U
                         ? arguments.evaluation_maximum_deals_per_co_class
                         : evaluation_k * 2U;
    }

    const Json result{
        {"schema", "gtosd.hu_preflop_tre_validation.v1"},
        {"status", precision_target_met ? "STATISTICAL_PRECISION_TARGET_MET"
                                        : "MAXIMUM_EVALUATION_CORPUS_REACHED"},
        {"game_id", game_id},
        {"protocol",
         {{"training_corpus", "T"},
          {"response_search_corpus", "R"},
          {"evaluation_corpus", "E"},
          {"corpus_seeds_are_distinct", true},
          {"training_policy_frozen_before_R", true},
          {"responses_frozen_before_E", true},
          {"paired_common_random_numbers_on_E", true},
          {"policy_completion_contract", "uniform_unseen_v1"}}},
        {"training",
         {{"game", std::move(training_game)},
          {"algorithm", "linear_mccfr"},
          {"strategy", "average"},
          {"iterations", arguments.iterations},
          {"solver_seed", arguments.solver_seed},
          {"traversed_nodes", traversed_nodes},
          {"maximum_strategy_normalization_error", maximum_normalization_error}}},
        {"response_search",
         {{"game", std::move(response_game)},
          {"training_policy_coverage", std::move(training_on_response_coverage)},
          {"players", std::move(response_search)},
          {"best_response_method", "exact_dynamic_programming_on_finite_R"},
          {"exact_completed_policy_nashconv_ante", exact_response_corpus_nashconv},
          {"physical_best_response_error_bound", nullptr}}},
        {"evaluation",
         {{"sampling", "stratified_by_CO_hand_class_with_exact_physical_class_mass"},
          {"conditional_sampling", "independent_BTN_holes_and_complete_board_with_replacement"},
          {"planned_looks", look_count},
          {"completed_looks", evaluation_looks.size()},
          {"initial_deals_per_co_class", arguments.evaluation_initial_deals_per_co_class},
          {"maximum_deals_per_co_class", arguments.evaluation_maximum_deals_per_co_class},
          {"final_deals_per_co_class", evaluation_k},
          {"confidence_method", "normal_approximation_stratified_paired_bonferroni_v1"},
          {"family_wise_confidence_level", 1.0 - confidence.value().family_wise_alpha},
          {"simultaneous_metrics", confidence.value().simultaneous_metrics},
          {"bonferroni_comparisons", confidence.value().bonferroni_comparisons},
          {"per_interval_alpha", confidence.value().per_interval_alpha},
          {"critical_value", confidence.value().critical_value},
          {"normalized_half_width_target", arguments.evaluation_normalized_half_width_target},
          {"precision_target_met", precision_target_met},
          {"looks", std::move(evaluation_looks)},
          {"final", std::move(final_evaluation)}}},
        {"claims",
         {{"exact_best_response_on_R", true},
          {"candidate_response_gain_estimated_on_independent_E", true},
          {"abstract_empirical_R_nashconv_available", true},
          {"physical_nashconv_certified", false},
          {"candidate_gain_is_physical_exploitability_upper_bound", false}}},
        {"timing",
         {{"training_compile_seconds", training_compile_seconds},
          {"training_solve_seconds", training_solve_seconds},
          {"response_compile_seconds", response_compile_seconds},
          {"response_search_seconds", response_search_seconds},
          {"evaluation_compile_seconds", evaluation_compile_seconds},
          {"evaluation_seconds", evaluation_seconds},
          {"total_seconds", seconds_since(total_started)}}},
        {"limitations",
         {"The exact best responses optimize only the finite response corpus R.",
          "The E confidence intervals measure frozen candidate-response gains, not exhaustive "
          "physical best responses.",
          "Uniform completion on unseen information sets is part of the versioned policy contract "
          "and its coverage is reported.",
          "Normal confidence intervals are asymptotic; small K can have poor finite-sample "
          "coverage despite Bonferroni correction."}}};
    write_result(arguments.output, result.dump(2));
    std::cout << "HU_PREFLOP_TRE_VALIDATION=PASS output=" << arguments.output
              << " status=" << result.at("status").get<std::string>()
              << " evaluation_k=" << evaluation_k
              << " completed_looks=" << result.at("evaluation").at("completed_looks") << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HU_PREFLOP_TRE_VALIDATION=FAIL reason=" << error.what() << '\n';
    return 1;
  }
}
