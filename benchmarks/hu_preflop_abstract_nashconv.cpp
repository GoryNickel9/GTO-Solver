#include "gtosd/preflop/hu_preflop_abstract_game.hpp"
#include "gtosd/solver/best_response.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr double target_normalized_dev = 0.01;

struct Arguments {
  std::string config;
  std::string output;
  std::string checkpoint_input;
  std::string checkpoint_output;
  std::uint32_t deals_per_co_class{1U};
  std::uint32_t equity_samples{4U};
  std::uint64_t iterations{256U};
  std::uint64_t interval{128U};
  std::uint64_t solver_seed{0x5632'3353'4F4C'5601ULL};
  std::uint64_t corpus_seed{0x5632'3343'4F52'5001ULL};
  std::uint64_t partition_seed{0x5632'3350'4152'5401ULL};
  std::uint64_t maximum_nodes{1'000'000U};
  bool stop_on_target{true};
};

std::uint64_t parse_unsigned(const std::string_view value, const std::string_view name) {
  std::size_t consumed = 0U;
  const auto parsed = std::stoull(std::string{value}, &consumed, 0);
  if (consumed != value.size() || parsed == 0U) {
    throw std::runtime_error("invalid " + std::string{name});
  }
  return parsed;
}

Arguments parse_arguments(const int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name = argv[index];
    if (name == "--continue-after-target") {
      arguments.stop_on_target = false;
      continue;
    }
    if (index + 1 >= argc) {
      throw std::runtime_error("missing argument value");
    }
    const std::string_view value = argv[++index];
    if (name == "--config") {
      arguments.config = value;
    } else if (name == "--output") {
      arguments.output = value;
    } else if (name == "--checkpoint-input") {
      arguments.checkpoint_input = value;
    } else if (name == "--checkpoint-output") {
      arguments.checkpoint_output = value;
    } else if (name == "--deals-per-co-class") {
      const auto parsed = parse_unsigned(value, name);
      if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("deals per CO class exceed uint32");
      }
      arguments.deals_per_co_class = static_cast<std::uint32_t>(parsed);
    } else if (name == "--equity-samples") {
      const auto parsed = parse_unsigned(value, name);
      if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("equity samples exceed uint32");
      }
      arguments.equity_samples = static_cast<std::uint32_t>(parsed);
    } else if (name == "--iterations") {
      arguments.iterations = parse_unsigned(value, name);
    } else if (name == "--interval") {
      arguments.interval = parse_unsigned(value, name);
    } else if (name == "--solver-seed") {
      arguments.solver_seed = parse_unsigned(value, name);
    } else if (name == "--corpus-seed") {
      arguments.corpus_seed = parse_unsigned(value, name);
    } else if (name == "--partition-seed") {
      arguments.partition_seed = parse_unsigned(value, name);
    } else if (name == "--maximum-nodes") {
      arguments.maximum_nodes = parse_unsigned(value, name);
    } else {
      throw std::runtime_error("unknown argument " + std::string{name});
    }
  }
  if (arguments.config.empty() || arguments.output.empty() ||
      arguments.iterations < arguments.interval ||
      arguments.iterations % arguments.interval != 0U) {
    throw std::runtime_error(
        "--config and --output are required; iterations must be a multiple of interval");
  }
  return arguments;
}

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

std::string checkpoint_fingerprint(const gtosd::SolverCheckpoint &checkpoint) {
  const auto serialized = gtosd::serialize_solver_checkpoint(checkpoint);
  if (!serialized) {
    throw std::runtime_error("cannot serialize checkpoint");
  }
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const auto byte : serialized.value()) {
    hash ^= static_cast<std::uint8_t>(byte);
    hash *= 1'099'511'628'211ULL;
  }
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result = "fnv1a64:";
  for (int shift = 60; shift >= 0; shift -= 4) {
    result.push_back(digits[static_cast<std::size_t>((hash >> shift) & 0xFU)]);
  }
  return result;
}

std::uint64_t minimum_materialized_bytes(const gtosd::FiniteGame &game) {
  std::uint64_t bytes = game.nodes.size() * sizeof(gtosd::GameNode);
  for (const auto &node : game.nodes) {
    bytes += node.information_set.size();
    bytes += node.edges.size() * sizeof(gtosd::GameEdge);
    for (const auto &edge : node.edges) {
      bytes += edge.action.label.size();
    }
  }
  return bytes;
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

    gtosd::HuPreflopSolveOptions abstraction_options;
    abstraction_options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::
        DistributionalStrengthStreetAdaptivePerfectRecallV23;
    abstraction_options.equity_samples_per_bucket = arguments.equity_samples;
    abstraction_options.partition_seed = arguments.partition_seed;
    abstraction_options.distributional_bucket_capacities = {32U, 128U, 512U};
    const auto compile_started = Clock::now();
    auto compiled = gtosd::compile_hu_preflop_abstract_game(
        tree.value(), abstraction_options, arguments.deals_per_co_class, arguments.corpus_seed,
        arguments.maximum_nodes);
    const auto compile_seconds = seconds_since(compile_started);
    if (!compiled) {
      throw std::runtime_error(std::string{"abstract game compile failed: "} +
                               gtosd::hu_preflop_error_name(compiled.error()));
    }

    std::optional<gtosd::SolverCheckpoint> resume;
    if (!arguments.checkpoint_input.empty()) {
      const auto loaded = gtosd::load_solver_checkpoint(arguments.checkpoint_input);
      if (!loaded) {
        throw std::runtime_error(std::string{"cannot load Linear MCCFR checkpoint: "} +
                                 gtosd::solver_error_name(loaded.error()));
      }
      if (loaded.value().game_fingerprint != compiled.value().summary.fingerprint ||
          loaded.value().config.algorithm != gtosd::SolverAlgorithm::LinearMccfr ||
          loaded.value().config.seed != arguments.solver_seed ||
          loaded.value().completed_iterations >= arguments.iterations ||
          loaded.value().completed_iterations % arguments.interval != 0U) {
        throw std::runtime_error("checkpoint does not match game, solver seed or interval");
      }
      resume = loaded.value();
    }
    Json checkpoints = Json::array();
    double solve_seconds = 0.0;
    double certification_seconds = 0.0;
    double maximum_normalization_error = 0.0;
    std::uint64_t traversed_nodes = 0U;
    bool stopped_early_on_target = false;
    gtosd::NashConvResult final_metrics;
    std::optional<gtosd::StrategyProfile> final_strategy;
    const auto first_target =
        (resume.has_value() ? resume->completed_iterations : 0U) + arguments.interval;
    for (std::uint64_t target = first_target; target <= arguments.iterations;
         target += arguments.interval) {
      gtosd::SolverConfig solver;
      solver.algorithm = gtosd::SolverAlgorithm::LinearMccfr;
      solver.iterations = target;
      solver.seed = arguments.solver_seed;
      const auto solve_started = Clock::now();
      const auto solved = gtosd::solve_finite_game(compiled.value().game, solver,
                                                   resume.has_value() ? &resume.value() : nullptr);
      solve_seconds += seconds_since(solve_started);
      if (!solved) {
        throw std::runtime_error(std::string{"Linear MCCFR failed: "} +
                                 gtosd::solver_error_name(solved.error()));
      }
      const auto certification_started = Clock::now();
      const auto metrics =
          gtosd::calculate_nash_conv(compiled.value().game, solved.value().average_strategy);
      certification_seconds += seconds_since(certification_started);
      if (!metrics) {
        throw std::runtime_error(std::string{"exact NashConv failed: "} +
                                 gtosd::solver_error_name(metrics.error()));
      }
      std::array<double, 2> gains{};
      for (std::size_t player = 0U; player < gains.size(); ++player) {
        gains[player] =
            metrics.value().best_response_value[player] - metrics.value().profile_value[player];
        if (gains[player] < -1.0e-12) {
          throw std::runtime_error("exact best response underperforms the frozen profile");
        }
      }
      const auto normalized_dev = std::max(gains[0], gains[1]) / compiled.value().game.initial_pot;
      checkpoints.push_back(
          {{"iteration", target},
           {"profile_ev_ante", metrics.value().profile_value},
           {"best_response_ev_ante", metrics.value().best_response_value},
           {"deviation_gain_ante", gains},
           {"nashconv_ante", metrics.value().nash_conv},
           {"normalized_nashconv", metrics.value().normalized_nash_conv},
           {"normalized_dev", normalized_dev},
           {"checkpoint_fingerprint", checkpoint_fingerprint(solved.value().checkpoint)}});
      maximum_normalization_error =
          std::max(maximum_normalization_error, solved.value().maximum_normalization_error);
      traversed_nodes += solved.value().traversed_nodes;
      resume = solved.value().checkpoint;
      final_metrics = metrics.value();
      final_strategy = std::move(solved.value().average_strategy);
      if (arguments.stop_on_target && normalized_dev < target_normalized_dev) {
        stopped_early_on_target = target < arguments.iterations;
        break;
      }
    }
    if (!arguments.checkpoint_output.empty() &&
        !gtosd::save_solver_checkpoint(resume.value(), arguments.checkpoint_output)) {
      throw std::runtime_error("cannot save final Linear MCCFR checkpoint");
    }

    const std::array final_gains{
        final_metrics.best_response_value[0] - final_metrics.profile_value[0],
        final_metrics.best_response_value[1] - final_metrics.profile_value[1]};
    const auto final_normalized_dev =
        std::max(final_gains[0], final_gains[1]) / compiled.value().game.initial_pot;
    const auto target_met = final_normalized_dev < target_normalized_dev;
    const auto coverage_started = Clock::now();
    const auto completed =
        gtosd::complete_strategy_profile(compiled.value().game, final_strategy.value(),
                                         gtosd::PolicyCompletionRule::UniformUnseenV1);
    const auto coverage_seconds = seconds_since(coverage_started);
    if (!completed) {
      throw std::runtime_error(std::string{"policy completion audit failed: "} +
                               gtosd::solver_error_name(completed.error()));
    }
    const auto &coverage = completed.value().coverage;
    const Json result{
        {"schema", "gtosd.hu_preflop_v23_abstract_nashconv.v2"},
        {"status", target_met ? "CERTIFIED_ABSTRACT_EMPIRICAL_CHANCE_TARGET_MET"
                              : "CERTIFIED_ABSTRACT_EMPIRICAL_CHANCE_TARGET_NOT_MET"},
        {"nashconv_certified", true},
        {"physical_nashconv_certified", false},
        {"scope", "exact_finite_game_with_stratified_empirical_complete_deal_chance_v1"},
        {"game_id", game_id},
        {"identity",
         {{"rules_fingerprint", compiled.value().definition.rules_fingerprint},
          {"tree_fingerprint", compiled.value().definition.tree_fingerprint},
          {"abstraction_fingerprint", compiled.value().definition.abstraction_fingerprint},
          {"abstract_game_definition_fingerprint", compiled.value().definition.fingerprint},
          {"chance_corpus_fingerprint", compiled.value().chance_corpus.fingerprint},
          {"finite_game_fingerprint", compiled.value().summary.fingerprint}}},
        {"chance_corpus",
         {{"seed", arguments.corpus_seed},
          {"deals_per_co_class", arguments.deals_per_co_class},
          {"outcomes", compiled.value().chance_corpus.outcomes.size()},
          {"co_class_marginal", "exact_physical_combo_mass"},
          {"btn_holes_and_board", "conditional_empirical"}}},
        {"finite_game_census",
         {{"nodes", compiled.value().summary.nodes},
          {"terminal_nodes", compiled.value().summary.terminal_nodes},
          {"chance_nodes", compiled.value().summary.chance_nodes},
          {"decision_nodes", compiled.value().summary.decision_nodes},
          {"information_sets", compiled.value().summary.information_sets},
          {"maximum_depth", compiled.value().summary.maximum_depth},
          {"minimum_materialized_bytes", minimum_materialized_bytes(compiled.value().game)}}},
        {"solver",
         {{"algorithm", "linear_mccfr"},
          {"strategy", "average"},
          {"iterations", resume->completed_iterations},
          {"requested_iterations", arguments.iterations},
          {"certification_interval", arguments.interval},
          {"stop_on_target", arguments.stop_on_target},
          {"stopped_early_on_target", stopped_early_on_target},
          {"seed", arguments.solver_seed},
          {"resumed", !arguments.checkpoint_input.empty()},
          {"checkpoint_input",
           arguments.checkpoint_input.empty() ? Json(nullptr) : Json(arguments.checkpoint_input)},
          {"checkpoint_output",
           arguments.checkpoint_output.empty() ? Json(nullptr) : Json(arguments.checkpoint_output)},
          {"maximum_strategy_normalization_error", maximum_normalization_error},
          {"traversed_nodes", traversed_nodes}}},
        {"policy_completion",
         {{"contract", gtosd::policy_completion_rule_name(coverage.completion_rule)},
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
          {"reach_weighted_coverage_by_player", coverage.reach_weighted_coverage_by_player}}},
        {"final",
         {{"profile_ev_ante", final_metrics.profile_value},
          {"best_response_ev_ante", final_metrics.best_response_value},
          {"deviation_gain_ante", final_gains},
          {"nashconv_ante", final_metrics.nash_conv},
          {"normalized_nashconv", final_metrics.normalized_nash_conv},
          {"normalized_dev", final_normalized_dev},
          {"target_normalized_dev", target_normalized_dev},
          {"target_met", target_met}}},
        {"checkpoints", std::move(checkpoints)},
        {"timing",
         {{"compile_seconds", compile_seconds},
          {"linear_mccfr_seconds", solve_seconds},
          {"exact_nashconv_seconds", certification_seconds},
          {"policy_coverage_seconds", coverage_seconds},
          {"total_seconds", seconds_since(total_started)}}},
        {"limitations",
         {"Certificate applies only to this finite chance-corpus fingerprint.",
          "It does not certify the complete physical deal distribution.",
          "It does not transfer to another stack, corpus seed, K or action abstraction."}}};
    write_result(arguments.output, result.dump(2));
    std::cout << "V23_ABSTRACT_NASHCONV=PASS output=" << arguments.output
              << " nodes=" << compiled.value().summary.nodes
              << " infosets=" << compiled.value().summary.information_sets
              << " iterations=" << resume->completed_iterations
              << " normalized_dev=" << final_normalized_dev << " target_met=" << target_met
              << " stopped_early=" << stopped_early_on_target << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "V23_ABSTRACT_NASHCONV=FAIL reason=" << error.what() << '\n';
    return 1;
  }
}
