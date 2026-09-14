#include "gtosd/core/ranges.hpp"
#include "gtosd/preflop/hu_preflop.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using Json = nlohmann::json;

constexpr std::array<std::string_view, 5> root_action_ids{"all_in", "raise_6", "raise_10",
                                                          "call", "fold"};

std::uint64_t parse_integer(const std::string_view value, const std::string_view name) {
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0U) {
    throw std::runtime_error("invalid " + std::string(name));
  }
  return parsed;
}

std::vector<gtosd::HandClassId> parse_hand_classes(const std::string_view value) {
  std::vector<gtosd::HandClassId> output;
  std::array<bool, gtosd::hu_preflop_hand_class_count> seen{};
  std::size_t offset = 0U;
  while (offset < value.size()) {
    const auto delimiter = value.find(',', offset);
    const auto token = value.substr(
        offset, delimiter == std::string_view::npos ? value.size() - offset : delimiter - offset);
    if (token.empty()) {
      throw std::runtime_error("root decision trace contains an empty hand class");
    }
    auto matched = false;
    for (std::uint8_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
      if (gtosd::class_name(hand) != token) {
        continue;
      }
      if (seen[hand]) {
        throw std::runtime_error("root decision trace contains a duplicate hand class");
      }
      seen[hand] = true;
      output.push_back(hand);
      matched = true;
      break;
    }
    if (!matched) {
      throw std::runtime_error("unknown root decision trace hand class " + std::string(token));
    }
    if (delimiter == std::string_view::npos) {
      break;
    }
    offset = delimiter + 1U;
  }
  if (output.empty()) {
    throw std::runtime_error("root decision trace hand classes are empty");
  }
  return output;
}

struct Arguments {
  std::string config;
  std::string output;
  std::string postflop_policy_output;
  std::string action_conditioned_telemetry_output;
  std::string root_decision_trace_output;
  bool exact_preflop_all_in_expectation{false};
  gtosd::HuPreflopSolveOptions options;
};

Arguments parse_arguments(const int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string_view token = argv[index];
    if (token == "--postflop-exact") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
      continue;
    }
    if (token == "--postflop-current-street-buckets") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::CategoryEquityMonteCarloCurrentStreet;
      continue;
    }
    if (token == "--postflop-memoryless-buckets") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::CategoryEquityMonteCarloMemoryless;
      continue;
    }
    if (token == "--postflop-distributional-prototype") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype;
      continue;
    }
    if (token == "--postflop-distributional-perfect-recall") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPerfectRecall;
      continue;
    }
    if (token == "--postflop-distributional-bucket-history") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory;
      continue;
    }
    if (token == "--postflop-distributional-profile-v6") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6;
      continue;
    }
    if (token == "--postflop-distributional-structured-v7") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7;
      continue;
    }
    if (token == "--postflop-distributional-street-adaptive-v8") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8;
      continue;
    }
    if (token == "--postflop-distributional-selective-history-v9") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9;
      continue;
    }
    if (token == "--postflop-distributional-category-history-v10") {
      arguments.options.postflop_representation =
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10;
      continue;
    }
    if (token == "--postflop-distributional-adaptive-category-history-v11") {
      arguments.options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::
          DistributionalStrengthAdaptiveCategoryHistoryV11;
      continue;
    }
    if (token == "--reference-betting") {
      arguments.options.use_compiled_betting = false;
      continue;
    }
    if (token == "--opponent-value-baseline") {
      arguments.options.use_opponent_value_baseline = true;
      continue;
    }
    if (token == "--exact-preflop-all-in-expectation") {
      arguments.exact_preflop_all_in_expectation = true;
      continue;
    }
    if (token == "--exact-postflop-all-in-turn") {
      arguments.options.postflop_all_in_expectation_mode =
          gtosd::HuPreflopPostflopAllInExpectationMode::ExactTurn;
      continue;
    }
    if (token == "--exact-postflop-all-in-flop-turn") {
      arguments.options.postflop_all_in_expectation_mode =
          gtosd::HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn;
      continue;
    }
    if (token == "--public-board-stratified") {
      arguments.options.chance_sampling_mode =
          gtosd::HuPreflopChanceSamplingMode::PublicBoardStratified;
      continue;
    }
    if (token == "--full-preflop-chart-export") {
      arguments.options.evaluate_preflop_decisions = true;
      continue;
    }
    if (token == "--evaluate-current-profile") {
      arguments.options.evaluate_current_profile = true;
      continue;
    }
    if (token == "--action-conditioned-telemetry") {
      arguments.options.collect_action_conditioned_telemetry = true;
      continue;
    }
    if (token == "--root-continuation-mean-updates") {
      arguments.options.root_continuation_mean_updates = true;
      continue;
    }
    if (token == "--symmetric-traverser-mean-updates") {
      arguments.options.root_continuation_mean_updates = true;
      arguments.options.symmetric_traverser_mean_updates = true;
      continue;
    }
    if (token == "--root-common-random-numbers") {
      arguments.options.root_common_random_numbers = true;
      continue;
    }
    if (token == "--global-common-random-numbers") {
      arguments.options.global_common_random_numbers = true;
      continue;
    }
    if (token == "--root-first-opponent-response-stratification") {
      arguments.options.root_first_opponent_response_stratification = true;
      continue;
    }
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value after " + std::string(token));
    }
    const std::string_view value = argv[++index];
    if (token == "--config") {
      arguments.config = value;
    } else if (token == "--output") {
      arguments.output = value;
    } else if (token == "--postflop-policy-output") {
      arguments.postflop_policy_output = value;
    } else if (token == "--action-conditioned-telemetry-output") {
      arguments.action_conditioned_telemetry_output = value;
      arguments.options.collect_action_conditioned_telemetry = true;
    } else if (token == "--root-decision-trace-output") {
      arguments.root_decision_trace_output = value;
    } else if (token == "--root-decision-trace-classes") {
      arguments.options.root_decision_trace_hand_classes = parse_hand_classes(value);
    } else if (token == "--root-decision-trace-deals-per-class") {
      const auto parsed = parse_integer(value, "root decision trace deals per class");
      if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("root decision trace deals per class exceed uint32");
      }
      arguments.options.root_decision_trace_deals_per_class =
          static_cast<std::uint32_t>(parsed);
    } else if (token == "--iterations") {
      arguments.options.iterations = parse_integer(value, "iterations");
    } else if (token == "--preflop-refinement-iterations") {
      arguments.options.preflop_refinement_iterations =
          parse_integer(value, "preflop refinement iterations");
    } else if (token == "--evaluation-deals") {
      arguments.options.evaluation_deals = parse_integer(value, "evaluation deals");
    } else if (token == "--br-iterations") {
      arguments.options.best_response_iterations = parse_integer(value, "BR iterations");
    } else if (token == "--br-evaluation-deals") {
      arguments.options.best_response_evaluation_deals =
          parse_integer(value, "BR evaluation deals");
    } else if (token == "--equity-samples") {
      const auto parsed = parse_integer(value, "equity samples");
      if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("equity samples exceed uint32");
      }
      arguments.options.equity_samples_per_bucket = static_cast<std::uint32_t>(parsed);
    } else if (token == "--algorithm") {
      if (value == "external_sampling") {
        arguments.options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ExternalSampling;
      } else if (value == "linear_mccfr") {
        arguments.options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::LinearMccfr;
      } else if (value == "discounted_mccfr_1.5_0_3") {
        arguments.options.sampling_algorithm =
            gtosd::HuPreflopSamplingAlgorithm::DiscountedMccfr1503;
      } else if (value == "chance_sampled_cfr") {
        arguments.options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ChanceSampledCfr;
      } else {
        throw std::runtime_error("invalid algorithm");
      }
    } else if (token == "--seed") {
      arguments.options.seed = parse_integer(value, "seed");
    } else if (token == "--partition-seed") {
      arguments.options.partition_seed = parse_integer(value, "partition seed");
    } else if (token == "--evaluation-seed") {
      arguments.options.evaluation_seed = parse_integer(value, "evaluation seed");
    } else if (token == "--seven-card-table") {
      arguments.options.seven_card_table_path = value;
    } else if (token == "--maximum-numeric-state-bytes") {
      arguments.options.maximum_numeric_state_bytes =
          parse_integer(value, "maximum numeric state bytes");
    } else if (token == "--maximum-bucket-cache-entries") {
      arguments.options.maximum_bucket_cache_entries =
          parse_integer(value, "maximum bucket cache entries");
    } else if (token == "--maximum-exact-postflop-all-in-cache-entries") {
      arguments.options.maximum_exact_postflop_all_in_cache_entries =
          parse_integer(value, "maximum exact postflop all-in cache entries");
    } else if (token == "--maximum-action-conditioned-telemetry-entries") {
      arguments.options.maximum_action_conditioned_telemetry_entries =
          parse_integer(value, "maximum action-conditioned telemetry entries");
    } else if (token == "--threads") {
      const auto parsed = parse_integer(value, "threads");
      if (parsed > std::numeric_limits<std::uint8_t>::max()) {
        throw std::runtime_error("threads exceed uint8");
      }
      arguments.options.worker_threads = static_cast<std::uint8_t>(parsed);
    } else if (token == "--training-batch-iterations") {
      const auto parsed = parse_integer(value, "training batch iterations");
      if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("training batch iterations exceed uint32");
      }
      arguments.options.training_batch_iterations = static_cast<std::uint32_t>(parsed);
    } else if (token == "--root-action-value-rollouts") {
      const auto parsed = parse_integer(value, "root action value rollouts");
      if (parsed > std::numeric_limits<std::uint8_t>::max()) {
        throw std::runtime_error("root action value rollouts exceed uint8");
      }
      arguments.options.root_action_value_rollouts = static_cast<std::uint8_t>(parsed);
    } else if (token == "--maximum-parallel-updates-per-job") {
      const auto parsed = parse_integer(value, "maximum parallel updates per job");
      if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("maximum parallel updates per job exceeds uint32");
      }
      arguments.options.maximum_parallel_updates_per_job = static_cast<std::uint32_t>(parsed);
    } else if (token == "--maximum-parallel-scratch-bytes") {
      arguments.options.maximum_parallel_scratch_bytes =
          parse_integer(value, "maximum parallel scratch bytes");
    } else if (token == "--maximum-variance-baseline-bytes") {
      arguments.options.maximum_variance_baseline_bytes =
          parse_integer(value, "maximum variance baseline bytes");
    } else if (token == "--flop-buckets" || token == "--turn-buckets" ||
               token == "--river-buckets") {
      const auto parsed = parse_integer(value, "distributional bucket capacity");
      if (parsed >= gtosd::hu_preflop_sampled_postflop_unset_bucket) {
        throw std::runtime_error("distributional bucket capacity exceeds uint16 domain");
      }
      const auto street = token == "--flop-buckets" ? 0U : token == "--turn-buckets" ? 1U : 2U;
      arguments.options.distributional_bucket_capacities[street] =
          static_cast<std::uint16_t>(parsed);
    } else {
      throw std::runtime_error("unknown argument " + std::string(token));
    }
  }
  if (arguments.config.empty() || arguments.output.empty()) {
    throw std::runtime_error("--config and --output are required");
  }
  if (!arguments.root_decision_trace_output.empty() &&
      arguments.options.root_decision_trace_hand_classes.empty()) {
    throw std::runtime_error(
        "--root-decision-trace-output requires --root-decision-trace-classes");
  }
  arguments.options.export_postflop_policy = !arguments.postflop_policy_output.empty();
  return arguments;
}

std::string player_id(const std::uint8_t player) {
  if (player == 0U) {
    return "CO";
  }
  if (player == 1U) {
    return "BTN";
  }
  throw std::runtime_error("invalid preflop chart player");
}

std::string telemetry_terminal_id(const gtosd::HuPreflopTelemetryTerminalType type) {
  switch (type) {
  case gtosd::HuPreflopTelemetryTerminalType::PreflopFold:
    return "fold_preflop";
  case gtosd::HuPreflopTelemetryTerminalType::PreflopAllInExact:
    return "all_in_preflop_exact";
  case gtosd::HuPreflopTelemetryTerminalType::PreflopAllInSampled:
    return "all_in_preflop_sampled";
  case gtosd::HuPreflopTelemetryTerminalType::PostflopFold:
    return "fold_postflop";
  case gtosd::HuPreflopTelemetryTerminalType::PostflopAllInExact:
    return "all_in_postflop_exact";
  case gtosd::HuPreflopTelemetryTerminalType::PostflopAllInSampled:
    return "all_in_postflop_sampled";
  case gtosd::HuPreflopTelemetryTerminalType::PostflopShowdown:
    return "showdown_river";
  case gtosd::HuPreflopTelemetryTerminalType::PostflopContinuation:
    return "postflop_continuation";
  }
  throw std::runtime_error("unknown telemetry terminal type");
}

std::string telemetry_street_id(const gtosd::Street street) {
  switch (street) {
  case gtosd::Street::Preflop:
    return "preflop";
  case gtosd::Street::Flop:
    return "flop";
  case gtosd::Street::Turn:
    return "turn";
  case gtosd::Street::River:
    return "river";
  }
  throw std::runtime_error("unknown telemetry street");
}

std::string preflop_action_id(const gtosd::HuPreflopTree &tree, const gtosd::HuPreflopNode &node,
                              const gtosd::Action &action) {
  switch (action.type) {
  case gtosd::ActionType::Fold:
    return "fold";
  case gtosd::ActionType::Check:
    return "check";
  case gtosd::ActionType::Call:
    return "call";
  case gtosd::ActionType::AllIn:
    return "all_in";
  case gtosd::ActionType::Bet:
  case gtosd::ActionType::Raise: {
    const auto ante_units = tree.config.ante.units();
    const auto target_units =
        node.state.committed_this_street[node.state.player_to_act].units() + action.amount.units();
    if (ante_units <= 0 || target_units <= 0 || (target_units * 2) % ante_units != 0) {
      throw std::runtime_error("preflop raise target is not an exact half-ante");
    }
    const auto half_antes = (target_units * 2) / ante_units;
    if (half_antes % 2 == 0) {
      return "raise_" + std::to_string(half_antes / 2);
    }
    return "raise_" + std::to_string(half_antes / 2) + "_5";
  }
  }
  throw std::runtime_error("unknown preflop action type");
}

struct ChartHistoryStep {
  std::uint8_t player{0U};
  std::string action;
};

void collect_chart_histories(
    const gtosd::HuPreflopTree &tree, const std::uint32_t node_id,
    std::vector<ChartHistoryStep> &path,
    std::unordered_map<std::uint32_t, std::vector<ChartHistoryStep>> &histories) {
  if (node_id >= tree.nodes.size()) {
    throw std::runtime_error("chart history references an invalid node");
  }
  const auto [_, inserted] = histories.emplace(node_id, path);
  if (!inserted) {
    return;
  }
  const auto &node = tree.nodes[node_id];
  for (const auto &edge : node.edges) {
    path.push_back(
        ChartHistoryStep{node.state.player_to_act, preflop_action_id(tree, node, edge.action)});
    collect_chart_histories(tree, edge.child, path, histories);
    path.pop_back();
  }
}

Json serialize_full_preflop_charts(const gtosd::HuPreflopTree &tree,
                                   const gtosd::HuPreflopSolveResult &solved) {
  if (solved.preflop_decision_evaluations.size() != tree.stats.decision_nodes) {
    throw std::runtime_error("full preflop chart evaluation is incomplete");
  }
  std::vector<ChartHistoryStep> path;
  std::unordered_map<std::uint32_t, std::vector<ChartHistoryStep>> histories;
  collect_chart_histories(tree, tree.root, path, histories);

  Json output = Json::object();
  for (const auto &decision : solved.preflop_blueprint.decisions) {
    if (decision.node_id >= tree.nodes.size()) {
      throw std::runtime_error("preflop blueprint references an invalid node");
    }
    const auto &node = tree.nodes[decision.node_id];
    const auto evaluation = std::find_if(
        solved.preflop_decision_evaluations.begin(), solved.preflop_decision_evaluations.end(),
        [&decision](const auto &candidate) { return candidate.node_id == decision.node_id; });
    if (evaluation == solved.preflop_decision_evaluations.end() ||
        evaluation->player != decision.player ||
        evaluation->action_count != decision.action_count ||
        decision.action_count != node.edges.size()) {
      throw std::runtime_error("preflop chart strategy/evaluation mismatch");
    }
    const auto current_evaluation = std::find_if(
        solved.current_profile_preflop_decision_evaluations.begin(),
        solved.current_profile_preflop_decision_evaluations.end(),
        [&decision](const auto &candidate) { return candidate.node_id == decision.node_id; });
    if (solved.current_profile_evaluated &&
        (current_evaluation == solved.current_profile_preflop_decision_evaluations.end() ||
         current_evaluation->player != decision.player ||
         current_evaluation->action_count != decision.action_count)) {
      throw std::runtime_error("preflop current-profile chart/evaluation mismatch");
    }
    const auto training = std::find_if(
        solved.preflop_decision_training_diagnostics.begin(),
        solved.preflop_decision_training_diagnostics.end(),
        [&decision](const auto &candidate) { return candidate.node_id == decision.node_id; });
    if (training == solved.preflop_decision_training_diagnostics.end() ||
        training->player != decision.player || training->action_count != decision.action_count) {
      throw std::runtime_error("preflop chart training diagnostic mismatch");
    }

    Json history = Json::array();
    for (const auto &step : histories.at(decision.node_id)) {
      history.push_back({{"player", player_id(step.player)}, {"action", step.action}});
    }
    const auto chart_id = decision.node_id == tree.root
                              ? std::string{"CO"}
                              : "solver_node_" + std::to_string(decision.node_id);
    Json strategy = Json::object();
    Json current_strategy = Json::object();
    Json action_ev = Json::object();
    Json current_action_ev = Json::object();
    Json training_state = Json::object();
    for (std::size_t class_id = 0U; class_id < gtosd::hu_preflop_hand_class_count; ++class_id) {
      Json frequencies = Json::object();
      Json current_frequencies = Json::object();
      Json evs = Json::object();
      Json current_evs = Json::object();
      Json state_actions = Json::object();
      for (std::size_t action = 0U; action < node.edges.size(); ++action) {
        const auto identifier = preflop_action_id(tree, node, node.edges[action].action);
        frequencies[identifier] = decision.strategy[class_id][action];
        current_frequencies[identifier] = training->current_strategy[class_id][action];
        state_actions[identifier] = {
            {"cumulative_weighted_regret",
             training->cumulative_weighted_regret[class_id][action]},
            {"cumulative_average_weight",
             training->cumulative_average_weight[class_id][action]}};
        const auto effective_samples = evaluation->action_ev_effective_samples[class_id][action];
        if (effective_samples > 0.0) {
          evs[identifier] = {
              {"ev_ante", evaluation->action_ev_ante[class_id][action]},
              {"standard_error_ante", evaluation->action_ev_standard_error_ante[class_id][action]},
              {"samples", evaluation->action_ev_samples[class_id][action]},
              {"effective_samples", effective_samples}};
        } else {
          evs[identifier] = nullptr;
        }
        if (solved.current_profile_evaluated) {
          const auto current_effective_samples =
              current_evaluation->action_ev_effective_samples[class_id][action];
          if (current_effective_samples > 0.0) {
            current_evs[identifier] = {
                {"ev_ante", current_evaluation->action_ev_ante[class_id][action]},
                {"standard_error_ante",
                 current_evaluation->action_ev_standard_error_ante[class_id][action]},
                {"samples", current_evaluation->action_ev_samples[class_id][action]},
                {"effective_samples", current_effective_samples}};
          } else {
            current_evs[identifier] = nullptr;
          }
        }
      }
      strategy[gtosd::class_name(static_cast<gtosd::HandClassId>(class_id))] =
          std::move(frequencies);
      current_strategy[gtosd::class_name(static_cast<gtosd::HandClassId>(class_id))] =
          std::move(current_frequencies);
      action_ev[gtosd::class_name(static_cast<gtosd::HandClassId>(class_id))] = std::move(evs);
      if (solved.current_profile_evaluated) {
        current_action_ev[gtosd::class_name(static_cast<gtosd::HandClassId>(class_id))] =
            std::move(current_evs);
      }
      training_state[gtosd::class_name(static_cast<gtosd::HandClassId>(class_id))] = {
          {"last_update_iteration", training->last_iteration[class_id]},
          {"actions", std::move(state_actions)}};
    }
    output[chart_id] = {{"id", chart_id},
                        {"tree_node_id", decision.node_id},
                        {"player", player_id(decision.player)},
                        {"history", std::move(history)},
                        {"strategy", std::move(strategy)},
                        {"current_strategy", std::move(current_strategy)},
                        {"action_ev", std::move(action_ev)},
                        {"current_action_ev",
                         solved.current_profile_evaluated ? std::move(current_action_ev)
                                                          : Json(nullptr)},
                        {"training_state", std::move(training_state)},
                        {"training_state_scope",
                         "final_regret_matching_policy_and_linear_weighted_average_state"},
                        {"ev_scope", "forced_node_action_then_sampled_average_policy_continuation_"
                                     "opponent_path_importance_weighted_physical_deals"},
                        {"current_ev_scope",
                         solved.current_profile_evaluated
                             ? Json("forced_node_action_then_sampled_current_policy_continuation_"
                                    "opponent_path_importance_weighted_physical_deals")
                             : Json(nullptr)}};
  }
  if (output.size() != tree.stats.decision_nodes) {
    throw std::runtime_error("full preflop chart export has the wrong node count");
  }
  return output;
}

Json serialize_action_conditioned_telemetry(
    const gtosd::HuPreflopSolveResult &solved) {
  Json rows = Json::array();
  for (const auto &row : solved.action_conditioned_telemetry) {
    rows.push_back({
        {"node_id", row.node_id},
        {"player", player_id(row.player)},
        {"history", row.history},
        {"hand_class", gtosd::class_name(row.hand_class)},
        {"physical_combo_mass", row.physical_combo_mass},
        {"public_reach", row.public_reach},
        {"own_reach", row.own_reach},
        {"action_id", row.action_id},
        {"sample_count", row.sample_count},
        {"mean_action_value", row.mean_action_value},
        {"variance_action_value", row.variance_action_value},
        {"standard_error_action_value", row.standard_error_action_value},
        {"mean_action_advantage", row.mean_action_advantage},
        {"bucket_key", row.bucket_key},
        {"bucket_occupancy", row.bucket_occupancy},
        {"postflop_street", telemetry_street_id(row.postflop_street)},
        {"terminal_type", telemetry_terminal_id(row.terminal_type)},
        {"all_in_exact_count", row.all_in_exact_count},
        {"all_in_sampled_count", row.all_in_sampled_count},
        {"minimum_action_value", row.minimum_action_value},
        {"maximum_action_value", row.maximum_action_value},
        {"spread_action_value", row.spread_action_value}});
  }
  return rows;
}

Json serialize_trace_value(const gtosd::HuPreflopRootDecisionTraceValueSummary &value) {
  return {{"samples", value.samples},
          {"probability", value.probability},
          {"mean_payoff_ante", value.mean_payoff_ante},
          {"standard_error_ante", value.standard_error_ante},
          {"ev_contribution_ante", value.ev_contribution_ante}};
}

std::string trace_policy_view_id(const gtosd::HuPreflopRootDecisionTracePolicyView view) {
  switch (view) {
  case gtosd::HuPreflopRootDecisionTracePolicyView::Average:
    return "average";
  case gtosd::HuPreflopRootDecisionTracePolicyView::Current:
    return "current";
  }
  throw std::runtime_error("unknown root decision trace policy view");
}

Json serialize_root_decision_traces(const gtosd::HuPreflopTree &tree,
                                    const gtosd::HuPreflopSolveResult &solved) {
  Json rows = Json::array();
  for (const auto &trace : solved.root_decision_traces) {
    Json training_actions = Json::object();
    Json training_advantages = Json::object();
    Json training_pairwise = Json::object();
    for (std::size_t action = 0U; action < root_action_ids.size(); ++action) {
      const auto action_name = std::string(root_action_ids[action]);
      training_actions[action_name] = {
          {"average_strategy", trace.average_strategy[action]},
          {"current_strategy", trace.current_strategy[action]},
          {"cumulative_weighted_regret", trace.cumulative_weighted_regret[action]},
          {"positive_regret", std::max(0.0, trace.cumulative_weighted_regret[action])},
          {"cumulative_average_weight", trace.cumulative_average_weight[action]}};
      training_advantages[action_name] = {
          {"weighted_mean_ante", trace.training_action_advantage.weighted_mean_ante[action]},
          {"weighted_standard_deviation_ante",
           trace.training_action_advantage.weighted_standard_deviation_ante[action]},
          {"weighted_standard_error_ante",
           trace.training_action_advantage.weighted_standard_error_ante[action]},
          {"cumulative_regret_reconstruction_error_ante",
           trace.training_action_advantage.cumulative_regret_reconstruction_error_ante[action]}};
      Json paired_row = Json::object();
      for (std::size_t other = 0U; other < root_action_ids.size(); ++other) {
        paired_row[std::string(root_action_ids[other])] = {
            {"weighted_mean_ante",
             trace.training_action_advantage.pairwise_weighted_mean_ante[action][other]},
            {"weighted_standard_error_ante",
             trace.training_action_advantage
                 .pairwise_weighted_standard_error_ante[action][other]}};
      }
      training_pairwise[action_name] = std::move(paired_row);
    }

    Json policies = Json::object();
    for (const auto &policy : trace.policies) {
      Json actions = Json::object();
      for (const auto &action : policy.actions) {
        if (action.action_id >= root_action_ids.size()) {
          throw std::runtime_error("root decision trace action id is invalid");
        }
        Json branches = Json::array();
        for (const auto &branch : action.preflop_branches) {
          Json continuation = Json::array();
          for (const auto &step : branch.preflop_continuation) {
            if (step.node_id >= tree.nodes.size()) {
              throw std::runtime_error("root decision trace step node is invalid");
            }
            const auto &node = tree.nodes[step.node_id];
            if (node.kind != gtosd::HuPreflopNodeKind::Decision ||
                step.edge_index >= node.edges.size() || step.player != node.state.player_to_act) {
              throw std::runtime_error("root decision trace step edge is invalid");
            }
            continuation.push_back({
                {"node_id", step.node_id},
                {"player", player_id(step.player)},
                {"action", preflop_action_id(tree, node, node.edges[step.edge_index].action)}});
          }
          branches.push_back({
              {"preflop_continuation", std::move(continuation)},
              {"terminal_type", telemetry_terminal_id(branch.terminal_type)},
              {"terminal_street", telemetry_street_id(branch.terminal_street)},
              {"value", serialize_trace_value(branch.value)}});
        }
        Json streets = Json::array();
        for (const auto &street : action.street_reach) {
          streets.push_back({{"street", telemetry_street_id(street.street)},
                             {"value", serialize_trace_value(street.value)}});
        }
        Json buckets = Json::array();
        for (const auto &bucket : action.buckets) {
          buckets.push_back({{"street", telemetry_street_id(bucket.street)},
                             {"player", player_id(bucket.player)},
                             {"bucket_key", bucket.bucket_key},
                             {"value", serialize_trace_value(bucket.value)}});
        }
        actions[std::string(root_action_ids[action.action_id])] = {
            {"value", serialize_trace_value(action.value)},
            {"preflop_branches", std::move(branches)},
            {"street_reach", std::move(streets)},
            {"buckets", std::move(buckets)}};
      }
      Json paired = Json::object();
      for (std::size_t left = 0U; left < root_action_ids.size(); ++left) {
        Json paired_row = Json::object();
        for (std::size_t right = 0U; right < root_action_ids.size(); ++right) {
          paired_row[std::string(root_action_ids[right])] = {
              {"mean_ante", policy.paired_difference_mean_ante[left][right]},
              {"standard_error_ante",
               policy.paired_difference_standard_error_ante[left][right]}};
        }
        paired[std::string(root_action_ids[left])] = std::move(paired_row);
      }
      policies[trace_policy_view_id(policy.policy_view)] = {
          {"actions", std::move(actions)},
          {"paired_action_differences", std::move(paired)}};
    }
    rows.push_back({
        {"hand_class", gtosd::class_name(trace.hand_class)},
        {"deals_per_action", trace.deals_per_action},
        {"training_state",
         {{"last_update_iteration", trace.last_update_iteration},
          {"observations", trace.training_action_advantage.observations},
          {"weight_sum", trace.training_action_advantage.weight_sum},
          {"effective_samples", trace.training_action_advantage.effective_samples},
          {"actions", std::move(training_actions)},
          {"action_advantages", std::move(training_advantages)},
          {"paired_action_differences", std::move(training_pairwise)}}},
        {"policies", std::move(policies)}});
  }
  return rows;
}

int run(const int argc, char **argv) {
  auto arguments = parse_arguments(argc, argv);
  std::ifstream config_input(arguments.config, std::ios::binary);
  if (!config_input) {
    throw std::runtime_error("cannot open config");
  }
  const std::string serialized_config{std::istreambuf_iterator<char>(config_input),
                                      std::istreambuf_iterator<char>()};
  const auto config = gtosd::deserialize_hu_preflop_config_json(serialized_config);
  if (!config) {
    throw std::runtime_error(gtosd::hu_preflop_error_name(config.error()));
  }
  const auto tree = gtosd::build_hu_preflop_tree(config.value());
  if (!tree) {
    throw std::runtime_error(gtosd::hu_preflop_error_name(tree.error()));
  }
  double all_in_oracle_build_seconds = 0.0;
  if (arguments.exact_preflop_all_in_expectation) {
    std::cout << "HU_PREFLOP_EXACT_ALL_IN_ORACLE_BUILD_START\n" << std::flush;
    const auto started = std::chrono::steady_clock::now();
    const auto catalog = gtosd::build_hu_preflop_all_in_board_catalog();
    if (!catalog) {
      throw std::runtime_error(gtosd::hu_preflop_error_name(catalog.error()));
    }
    const auto table = gtosd::build_hu_preflop_all_in_equity_table(catalog.value());
    if (!table) {
      throw std::runtime_error(gtosd::hu_preflop_error_name(table.error()));
    }
    const auto oracle = gtosd::make_hu_preflop_all_in_training_oracle(table.value());
    if (!oracle) {
      throw std::runtime_error(gtosd::hu_preflop_error_name(oracle.error()));
    }
    arguments.options.preflop_all_in_training_oracle =
        std::make_shared<const gtosd::HuPreflopAllInTrainingOracle>(oracle.value());
    all_in_oracle_build_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "HU_PREFLOP_EXACT_ALL_IN_ORACLE=READY fingerprint="
              << table.value().fingerprint
              << " seconds=" << all_in_oracle_build_seconds << '\n';
  }
  std::cout
      << "HU_PREFLOP_SOLVE_START"
      << " iterations=" << arguments.options.iterations
      << " preflop_refinement_iterations=" << arguments.options.preflop_refinement_iterations
      << " evaluation_deals=" << arguments.options.evaluation_deals
      << " br_iterations=" << arguments.options.best_response_iterations
      << " br_evaluation_deals=" << arguments.options.best_response_evaluation_deals
      << " equity_samples=" << arguments.options.equity_samples_per_bucket << " algorithm="
      << (arguments.options.sampling_algorithm ==
                  gtosd::HuPreflopSamplingAlgorithm::ExternalSampling
              ? "external_sampling"
          : arguments.options.sampling_algorithm == gtosd::HuPreflopSamplingAlgorithm::LinearMccfr
              ? "linear_mccfr"
          : arguments.options.sampling_algorithm ==
                  gtosd::HuPreflopSamplingAlgorithm::ChanceSampledCfr
              ? "chance_sampled_cfr"
              : "discounted_mccfr_1.5_0_3")
      << " postflop_representation="
      << (arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::ExactPhysical
              ? "exact_physical"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::CategoryEquityMonteCarloCurrentStreet
              ? "category_equity_mc_current_street"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::CategoryEquityMonteCarloMemoryless
              ? "category_equity_mc_memoryless"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype
              ? "distributional_strength_prototype"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPerfectRecall
              ? "distributional_strength_perfect_recall"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory
              ? "distributional_strength_bucket_history"
          : arguments.options.postflop_representation == gtosd::HuPreflopPostflopRepresentation::
                                                              DistributionalStrengthAdaptiveCategoryHistoryV11
              ? "distributional_strength_adaptive_category_history_v11"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10
              ? "distributional_strength_category_history_v10"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9
              ? "distributional_strength_selective_history_v9"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8
              ? "distributional_strength_street_adaptive_v8"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7
              ? "distributional_strength_structured_v7"
          : arguments.options.postflop_representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6
              ? "distributional_strength_profile_v6"
              : "category_equity_mc_perfect_recall")
      << " training_seed=" << arguments.options.seed
      << " partition_seed=" << arguments.options.partition_seed
      << " evaluation_seed=" << arguments.options.evaluation_seed << " evaluator="
      << (arguments.options.seven_card_table_path.empty() ? "oracle" : "seven_card_table")
      << " betting=" << (arguments.options.use_compiled_betting ? "compiled" : "reference")
      << " numeric_state_budget_bytes=" << arguments.options.maximum_numeric_state_bytes
      << " bucket_cache_budget_entries=" << arguments.options.maximum_bucket_cache_entries
      << " exact_postflop_all_in_cache_budget_entries="
      << arguments.options.maximum_exact_postflop_all_in_cache_entries
      << " worker_threads=" << static_cast<unsigned>(arguments.options.worker_threads)
      << " training_batch_iterations=" << arguments.options.training_batch_iterations
      << " root_action_value_rollouts="
      << static_cast<unsigned>(arguments.options.root_action_value_rollouts)
      << " root_continuation_mean_updates="
      << arguments.options.root_continuation_mean_updates
      << " symmetric_traverser_mean_updates="
      << arguments.options.symmetric_traverser_mean_updates
      << " root_common_random_numbers="
      << arguments.options.root_common_random_numbers
      << " global_common_random_numbers="
      << arguments.options.global_common_random_numbers
      << " root_first_opponent_response_stratification="
      << arguments.options.root_first_opponent_response_stratification
      << " evaluate_current_profile=" << arguments.options.evaluate_current_profile
      << " maximum_parallel_updates_per_job=" << arguments.options.maximum_parallel_updates_per_job
      << " maximum_parallel_scratch_bytes=" << arguments.options.maximum_parallel_scratch_bytes
      << " opponent_value_baseline=" << arguments.options.use_opponent_value_baseline
      << " exact_preflop_all_in_expectation="
      << arguments.exact_preflop_all_in_expectation
      << " postflop_all_in_expectation="
      << (arguments.options.postflop_all_in_expectation_mode ==
                  gtosd::HuPreflopPostflopAllInExpectationMode::ExactTurn
              ? "exact_turn"
          : arguments.options.postflop_all_in_expectation_mode ==
                  gtosd::HuPreflopPostflopAllInExpectationMode::ExactFlopAndTurn
              ? "exact_flop_turn"
              : "sampled_runout")
      << " chance_sampling="
      << (arguments.options.chance_sampling_mode ==
                  gtosd::HuPreflopChanceSamplingMode::PublicBoardStratified
              ? "public_board_stratified"
              : "independent_physical")
      << " maximum_variance_baseline_bytes=" << arguments.options.maximum_variance_baseline_bytes
      << " distributional_capacities=" << arguments.options.distributional_bucket_capacities[0]
      << ',' << arguments.options.distributional_bucket_capacities[1] << ','
      << arguments.options.distributional_bucket_capacities[2]
      << " telemetry_max_entries="
      << arguments.options.maximum_action_conditioned_telemetry_entries
      << " root_decision_trace_classes="
      << arguments.options.root_decision_trace_hand_classes.size()
      << " root_decision_trace_deals_per_class="
      << arguments.options.root_decision_trace_deals_per_class << '\n';
  const auto solved = gtosd::solve_hu_preflop_sampled(tree.value(), arguments.options);
  if (!solved) {
    throw std::runtime_error(gtosd::hu_preflop_error_name(solved.error()));
  }
  if (!arguments.postflop_policy_output.empty()) {
    const auto saved = gtosd::save_hu_preflop_sampled_postflop_policy(
        solved.value().postflop_policy, arguments.postflop_policy_output);
    if (!saved) {
      throw std::runtime_error(std::string{"cannot save sampled postflop policy: "} +
                               std::string{gtosd::hu_preflop_error_name(saved.error())});
    }
  }

  Json strategy = Json::object();
  Json root_action_ev = Json::object();
  Json current_profile_root_action_ev = Json::object();
  Json root_regret_diagnostics = Json::object();
  Json root_action_advantage_diagnostics = Json::object();
  for (std::uint8_t class_id = 0; class_id < 81U; ++class_id) {
    Json row = Json::object();
    Json ev_row = Json::object();
    Json current_ev_row = Json::object();
    Json diagnostic_actions = Json::object();
    Json advantage_actions = Json::object();
    Json pairwise_advantages = Json::object();
    const auto &advantage = solved.value().root_action_advantage_diagnostics[class_id];
    for (std::size_t action = 0; action < root_action_ids.size(); ++action) {
      row[std::string(root_action_ids[action])] = solved.value().root_strategy[class_id][action];
      ev_row[std::string(root_action_ids[action])] = {
          {"ev_ante", solved.value().root_action_ev_ante[class_id][action]},
          {"standard_error_ante",
           solved.value().root_action_ev_standard_error_ante[class_id][action]},
          {"samples", solved.value().root_action_ev_samples[class_id][action]}};
      if (solved.value().current_profile_evaluated) {
        current_ev_row[std::string(root_action_ids[action])] = {
            {"ev_ante", solved.value().current_profile_root_action_ev_ante[class_id][action]},
            {"standard_error_ante",
             solved.value().current_profile_root_action_ev_standard_error_ante[class_id][action]},
            {"samples",
             solved.value().current_profile_root_action_ev_samples[class_id][action]}};
      }
      diagnostic_actions[std::string(root_action_ids[action])] = {
          {"average_strategy", solved.value().root_strategy[class_id][action]},
          {"current_strategy", solved.value().root_current_strategy[class_id][action]},
          {"cumulative_weighted_regret",
           solved.value().root_cumulative_weighted_regret[class_id][action]},
          {"positive_regret",
           std::max(0.0, solved.value().root_cumulative_weighted_regret[class_id][action])},
          {"cumulative_average_weight",
           solved.value().root_cumulative_average_weight[class_id][action]}};
      advantage_actions[std::string(root_action_ids[action])] = {
          {"weighted_mean_ante", advantage.weighted_mean_ante[action]},
          {"weighted_standard_deviation_ante", advantage.weighted_standard_deviation_ante[action]},
          {"weighted_standard_error_ante", advantage.weighted_standard_error_ante[action]},
          {"cumulative_regret_reconstruction_error_ante",
           advantage.cumulative_regret_reconstruction_error_ante[action]}};
      Json pairwise_row = Json::object();
      for (std::size_t other = 0; other < root_action_ids.size(); ++other) {
        pairwise_row[std::string(root_action_ids[other])] = {
            {"weighted_mean_ante", advantage.pairwise_weighted_mean_ante[action][other]},
            {"weighted_standard_error_ante",
             advantage.pairwise_weighted_standard_error_ante[action][other]}};
      }
      pairwise_advantages[std::string(root_action_ids[action])] = std::move(pairwise_row);
    }
    strategy[gtosd::class_name(class_id)] = std::move(row);
    root_action_ev[gtosd::class_name(class_id)] = std::move(ev_row);
    if (solved.value().current_profile_evaluated) {
      current_profile_root_action_ev[gtosd::class_name(class_id)] =
          std::move(current_ev_row);
    }
    root_regret_diagnostics[gtosd::class_name(class_id)] = {
        {"last_update_iteration", solved.value().root_information_last_iteration[class_id]},
        {"actions", std::move(diagnostic_actions)}};
    root_action_advantage_diagnostics[gtosd::class_name(class_id)] = {
        {"observations", advantage.observations},
        {"weight_sum", advantage.weight_sum},
        {"effective_samples", advantage.effective_samples},
        {"actions", std::move(advantage_actions)},
        {"pairwise_action_differences", std::move(pairwise_advantages)}};
  }
  const auto &rake = config.value().rake;
  const auto rake_mode = rake.enabled
                             ? "enabled_" + std::to_string(rake.percentage.basis_points()) +
                                   "bp_cap_" + std::to_string(rake.cap.units()) + "u_nfnd_" +
                                   std::to_string(rake.no_flop_no_drop)
                             : std::string{"disabled"};
  constexpr double normal_95 = 1.959963984540054;
  const auto sampled_response_value_sum =
      solved.value().best_response_co_ev_ante + solved.value().best_response_btn_ev_ante;
  const auto sampled_response_value_sum_standard_error =
      std::hypot(solved.value().best_response_co_standard_error_ante,
                 solved.value().best_response_btn_standard_error_ante);
  const std::array sampled_response_value_sum_confidence_interval{
      sampled_response_value_sum - normal_95 * sampled_response_value_sum_standard_error,
      sampled_response_value_sum + normal_95 * sampled_response_value_sum_standard_error};
  Json candidate{
      {"schema", "gtosd.hu_preflop_candidate.v1"},
      {"benchmark_id", "GTP-HU-PREFLOP-CO40-001"},
      {"game_config", arguments.config},
      {"tree_fingerprint", solved.value().tree_fingerprint},
      {"rake_mode", rake_mode},
      {"rake",
       {{"enabled", rake.enabled},
        {"percentage_bp", rake.percentage.basis_points()},
        {"cap_units", rake.cap.units()},
        {"no_flop_no_drop", rake.no_flop_no_drop},
        {"minimum_pot_units", rake.minimum_pot.units()}}},
      {"root_ev_ante", solved.value().root_ev_ante},
      {"root_ev_standard_error_ante", solved.value().root_ev_standard_error_ante},
      {"current_profile_evaluated", solved.value().current_profile_evaluated},
      {"action_conditioned_telemetry_enabled",
       solved.value().action_conditioned_telemetry_enabled},
      {"action_conditioned_telemetry_entries",
       solved.value().action_conditioned_telemetry.size()},
      {"action_conditioned_telemetry_dropped",
       solved.value().action_conditioned_telemetry_dropped},
      {"root_decision_trace_enabled", !solved.value().root_decision_traces.empty()},
      {"root_decision_trace_classes", solved.value().root_decision_traces.size()},
      {"root_decision_trace_deals_per_class",
       arguments.options.root_decision_trace_deals_per_class},
      {"current_profile_root_ev_ante",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_root_ev_ante)
           : Json(nullptr)},
      {"current_profile_root_ev_standard_error_ante",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_root_ev_standard_error_ante)
           : Json(nullptr)},
      {"normalized_nashconv", solved.value().normalized_abstract_nashconv},
      {"nashconv_certified", false},
      {"strategy", std::move(strategy)},
      {"root_action_ev", std::move(root_action_ev)},
      {"current_profile_root_action_ev",
       solved.value().current_profile_evaluated ? std::move(current_profile_root_action_ev)
                                                : Json(nullptr)},
      {"root_regret_diagnostics", std::move(root_regret_diagnostics)},
      {"root_regret_diagnostics_scope",
       "linear_weighted_training_state_current_policy_is_positive_regret_normalized_"
       "average_policy_is_cumulative_average_weight_normalized"},
      {"root_action_advantage_diagnostics", std::move(root_action_advantage_diagnostics)},
      {"root_action_advantage_diagnostics_scope",
       "main_training_co_traverser_root_samples_linear_weighted_paired_welford_"
       "descriptive_nonstationary_serially_dependent_excludes_refinement_response_and_evaluation"},
      {"root_action_ev_scope",
       "forced_root_action_then_sampled_average_policy_continuation_common_physical_deals"},
      {"current_profile_root_action_ev_scope",
       solved.value().current_profile_evaluated
           ? Json("forced_root_action_then_sampled_current_policy_continuation_common_physical_"
                  "deals")
           : Json(nullptr)},
      {"algorithm", solved.value().algorithm_id},
      {"abstraction", solved.value().abstraction_id},
      {"iterations", solved.value().iterations},
      {"postflop_training_iterations", solved.value().postflop_training_iterations},
      {"preflop_refinement_iterations", solved.value().preflop_refinement_iterations},
      {"seed", solved.value().seed},
      {"partition_seed", solved.value().partition_seed},
      {"evaluation_seed", solved.value().evaluation_seed},
      {"evaluator_backend", solved.value().evaluator_backend_id},
      {"evaluator_table_payload_bytes", solved.value().evaluator_table_payload_bytes},
      {"winner_cache_hits", solved.value().winner_cache_hits},
      {"winner_cache_misses", solved.value().winner_cache_misses},
      {"numeric_state_payload_bytes", solved.value().numeric_state_payload_bytes},
      {"numeric_state_budget_bytes", solved.value().numeric_state_budget_bytes},
      {"exact_postflop_all_in_cache_budget_entries",
       arguments.options.maximum_exact_postflop_all_in_cache_entries},
      {"bucket_cache_peak_entries", solved.value().bucket_cache_peak_entries},
      {"bucket_cache_evictions", solved.value().bucket_cache_evictions},
      {"bucket_mapping_visits", solved.value().bucket_mapping_visits},
      {"bucket_mapping_computations", solved.value().bucket_mapping_computations},
      {"occupied_distributional_buckets", solved.value().occupied_distributional_buckets},
      {"distributional_bucket_capacities", solved.value().distributional_bucket_capacities},
      {"average_policy_queries", solved.value().average_policy_queries},
      {"untrained_average_policy_queries", solved.value().untrained_average_policy_queries},
      {"bucket_mapping_seconds", solved.value().bucket_mapping_seconds},
      {"postflop_action_value_spread_samples", solved.value().postflop_action_value_spread_samples},
      {"mean_postflop_action_value_spread_ante",
       solved.value().mean_postflop_action_value_spread_ante},
      {"maximum_postflop_action_value_spread_ante",
       solved.value().maximum_postflop_action_value_spread_ante},
      {"worker_threads", solved.value().worker_threads},
      {"training_batch_iterations", solved.value().training_batch_iterations},
      {"root_action_value_rollouts", solved.value().root_action_value_rollouts},
      {"root_continuation_mean_updates", solved.value().root_continuation_mean_updates},
      {"symmetric_traverser_mean_updates", solved.value().symmetric_traverser_mean_updates},
      {"root_common_random_numbers", solved.value().root_common_random_numbers},
      {"global_common_random_numbers", solved.value().global_common_random_numbers},
      {"root_first_opponent_response_stratification",
       solved.value().root_first_opponent_response_stratification},
      {"evaluate_current_profile", arguments.options.evaluate_current_profile},
      {"peak_parallel_updates_per_job", solved.value().peak_parallel_updates_per_job},
      {"peak_parallel_shadow_updates_per_worker",
       solved.value().peak_parallel_shadow_updates_per_worker},
      {"peak_parallel_scratch_payload_bytes", solved.value().peak_parallel_scratch_payload_bytes},
      {"variance_baseline_information_sets", solved.value().variance_baseline_information_sets},
      {"variance_baseline_payload_bytes", solved.value().variance_baseline_payload_bytes},
      {"exact_preflop_all_in_expectation",
       solved.value().exact_preflop_all_in_expectation},
      {"preflop_all_in_equity_table_fingerprint",
       solved.value().preflop_all_in_equity_table_fingerprint},
      {"preflop_all_in_oracle_build_seconds", all_in_oracle_build_seconds},
      {"postflop_all_in_expectation", solved.value().postflop_all_in_expectation_id},
      {"exact_postflop_all_in_evaluations",
       solved.value().exact_postflop_all_in_evaluations},
      {"exact_postflop_all_in_runouts", solved.value().exact_postflop_all_in_runouts},
      {"exact_postflop_all_in_seconds", solved.value().exact_postflop_all_in_seconds},
      {"exact_postflop_all_in_cache_hits",
       solved.value().exact_postflop_all_in_cache_hits},
      {"exact_postflop_all_in_cache_misses",
       solved.value().exact_postflop_all_in_cache_misses},
      {"exact_postflop_all_in_cache_peak_entries",
       solved.value().exact_postflop_all_in_cache_peak_entries},
      {"exact_postflop_all_in_cache_evictions",
       solved.value().exact_postflop_all_in_cache_evictions},
      {"chance_sampling", solved.value().chance_sampling_id},
      {"compiled_betting_nodes", solved.value().compiled_betting_nodes},
      {"compiled_betting_bytes", solved.value().compiled_betting_bytes},
      {"information_sets", solved.value().information_sets},
      {"best_response_information_sets", solved.value().best_response_information_sets},
      {"current_profile_best_response_information_sets",
       solved.value().current_profile_best_response_information_sets},
      {"bytes_per_information_set_payload", solved.value().bytes_per_information_set_payload},
      {"minimum_blueprint_payload_bytes", solved.value().minimum_blueprint_payload_bytes},
      {"minimum_best_response_payload_bytes", solved.value().minimum_best_response_payload_bytes},
      {"allocator_overhead_included", solved.value().allocator_overhead_included},
      {"solve_seconds", solved.value().solve_seconds},
      {"abstract_nashconv_ante", solved.value().abstract_nashconv_ante},
      {"best_response_co_ev_ante", solved.value().best_response_co_ev_ante},
      {"best_response_btn_ev_ante", solved.value().best_response_btn_ev_ante},
      {"best_response_co_standard_error_ante",
       solved.value().best_response_co_standard_error_ante},
      {"best_response_btn_standard_error_ante",
       solved.value().best_response_btn_standard_error_ante},
      {"sampled_response_lower_bound_ante",
       solved.value().sampled_response_lower_bound_ante},
      {"normalized_sampled_response_lower_bound",
       solved.value().normalized_sampled_response_lower_bound},
      {"current_profile_best_response_co_ev_ante",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_best_response_co_ev_ante)
           : Json(nullptr)},
      {"current_profile_best_response_btn_ev_ante",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_best_response_btn_ev_ante)
           : Json(nullptr)},
      {"current_profile_best_response_co_standard_error_ante",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_best_response_co_standard_error_ante)
           : Json(nullptr)},
      {"current_profile_best_response_btn_standard_error_ante",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_best_response_btn_standard_error_ante)
           : Json(nullptr)},
      {"current_profile_sampled_response_lower_bound_ante",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_sampled_response_lower_bound_ante)
           : Json(nullptr)},
      {"current_profile_normalized_sampled_response_lower_bound",
       solved.value().current_profile_evaluated
           ? Json(solved.value().current_profile_normalized_sampled_response_lower_bound)
           : Json(nullptr)},
      {"best_response_iterations", arguments.options.best_response_iterations},
      {"best_response_evaluation_deals", arguments.options.best_response_evaluation_deals},
      {"nashconv_scope",
       "legacy_sum_of_sampled_response_values_not_certified_use_sampled_response_lower_bound"},
      {"sampled_response_lower_bound_scope",
       "sum_of_individually_clamped_sampled_response_improvements_not_exact_best_response_not_"
       "certified"},
      {"nashconv_validation",
       {{"status", "ESTIMATED_LOWER_BOUND_ONLY"},
        {"certified", false},
        {"target_strategy", "average"},
        {"method", "independent_mccfr_response_training_holdout_evaluation_v1"},
        {"response_value_sum_ante", sampled_response_value_sum},
        {"response_value_sum_standard_error_ante",
         sampled_response_value_sum_standard_error},
        {"response_value_sum_normal_95_confidence_interval_ante",
         sampled_response_value_sum_confidence_interval},
        {"confidence_scope", "conditional_on_the_two_frozen_sampled_response_policies"},
        {"limitation",
         "feasible_sampled_responses_lower_bound_true_nashconv_but_do_not_upper_bound_"
         "optimization_error"}}},
  };
  if (!arguments.postflop_policy_output.empty()) {
    candidate["postflop_policy_file"] = arguments.postflop_policy_output;
    candidate["postflop_policy_fingerprint"] = solved.value().postflop_policy.fingerprint;
    candidate["postflop_policy_format_minor"] = solved.value().postflop_policy.minor;
    candidate["postflop_current_policy_present"] =
        solved.value().postflop_policy.current_policy_present;
  }
  if (solved.value().action_conditioned_telemetry_enabled) {
    candidate["action_conditioned_telemetry_scope"] =
        "training_only_non_mutating_action_conditioned_physical_deal_observations_v1";
    if (!arguments.action_conditioned_telemetry_output.empty()) {
      std::ofstream telemetry_output(arguments.action_conditioned_telemetry_output,
                                     std::ios::binary | std::ios::trunc);
      if (!telemetry_output) {
        throw std::runtime_error("cannot open action-conditioned telemetry output");
      }
      const Json telemetry = {
          {"schema", "gtosd.hu_preflop_action_conditioned_telemetry.v1"},
          {"tree_fingerprint", solved.value().tree_fingerprint},
          {"algorithm", solved.value().algorithm_id},
          {"abstraction", solved.value().abstraction_id},
          {"iterations", solved.value().iterations},
          {"seed", solved.value().seed},
          {"partition_seed", solved.value().partition_seed},
          {"evaluator_backend", solved.value().evaluator_backend_id},
          {"maximum_entries", arguments.options.maximum_action_conditioned_telemetry_entries},
          {"dropped_observations", solved.value().action_conditioned_telemetry_dropped},
          {"rows", serialize_action_conditioned_telemetry(solved.value())}};
      telemetry_output << telemetry.dump(2) << '\n';
      if (!telemetry_output) {
        throw std::runtime_error("cannot write action-conditioned telemetry output");
      }
      candidate["action_conditioned_telemetry_file"] =
          arguments.action_conditioned_telemetry_output;
    }
  }
  if (!solved.value().root_decision_traces.empty()) {
    const Json traces = serialize_root_decision_traces(tree.value(), solved.value());
    candidate["root_decision_trace_scope"] =
        "post_training_non_mutating_conditional_physical_deals_forced_root_action_"
        "paired_common_random_numbers_average_and_optional_current_policy_v1";
    if (arguments.root_decision_trace_output.empty()) {
      candidate["root_decision_traces"] = traces;
    } else {
      std::ofstream trace_output(arguments.root_decision_trace_output,
                                 std::ios::binary | std::ios::trunc);
      if (!trace_output) {
        throw std::runtime_error("cannot open root decision trace output");
      }
      const Json trace_document = {
          {"schema", "gtosd.hu_preflop_root_decision_trace.v1"},
          {"tree_fingerprint", solved.value().tree_fingerprint},
          {"algorithm", solved.value().algorithm_id},
          {"abstraction", solved.value().abstraction_id},
          {"iterations", solved.value().iterations},
          {"seed", solved.value().seed},
          {"partition_seed", solved.value().partition_seed},
          {"evaluation_seed", solved.value().evaluation_seed},
          {"evaluator_backend", solved.value().evaluator_backend_id},
          {"scope", candidate["root_decision_trace_scope"]},
          {"rows", traces}};
      trace_output << trace_document.dump(2) << '\n';
      if (!trace_output) {
        throw std::runtime_error("cannot write root decision trace output");
      }
      candidate["root_decision_trace_file"] = arguments.root_decision_trace_output;
    }
  }
  if (arguments.options.evaluate_preflop_decisions) {
    candidate["preflop_nodes"] = serialize_full_preflop_charts(tree.value(), solved.value());
    candidate["preflop_node_count"] = solved.value().preflop_decision_evaluations.size();
    candidate["preflop_node_ev_scope"] =
        "actor_class_and_public_path_conditioned_opponent_path_importance_weighted_v1";
  }
  std::ofstream output(arguments.output, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot open output");
  }
  output << candidate.dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("cannot write output");
  }
  std::cout << "HU_PREFLOP_SOLVE=PASS"
            << " output=" << arguments.output << " root_ev_ante=" << solved.value().root_ev_ante
            << " root_ev_se_ante=" << solved.value().root_ev_standard_error_ante
            << " normalized_abstract_nashconv=" << solved.value().normalized_abstract_nashconv
            << " infosets=" << solved.value().information_sets
            << " seconds=" << solved.value().solve_seconds << '\n';
  return 0;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "HU_PREFLOP_SOLVE=FAIL reason=" << error.what() << '\n';
    return 1;
  }
}
