#include "gtosd/preflop/hu_preflop_legacy.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Arguments {
  std::string config;
  std::string candidate;
  std::string policy;
  std::string output;
  double maximum_exact_seconds{7.0 * 24.0 * 60.0 * 60.0};
};

std::string read_file(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open " + path);
  }
  return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

Arguments parse_arguments(const int argc, char **argv) {
  Arguments result;
  for (int index = 1; index < argc; ++index) {
    if (index + 1 >= argc) {
      throw std::runtime_error("missing argument value");
    }
    const std::string_view name = argv[index++];
    const std::string value = argv[index];
    if (name == "--config") {
      result.config = value;
    } else if (name == "--candidate") {
      result.candidate = value;
    } else if (name == "--policy") {
      result.policy = value;
    } else if (name == "--output") {
      result.output = value;
    } else if (name == "--maximum-exact-seconds") {
      std::size_t parsed = 0U;
      result.maximum_exact_seconds = std::stod(value, &parsed);
      if (parsed != value.size() || !(result.maximum_exact_seconds > 0.0) ||
          !std::isfinite(result.maximum_exact_seconds)) {
        throw std::runtime_error("invalid maximum exact seconds");
      }
    } else {
      throw std::runtime_error("unknown argument " + std::string{name});
    }
  }
  if (result.config.empty() || result.candidate.empty() || result.policy.empty() ||
      result.output.empty()) {
    throw std::runtime_error("--config, --candidate, --policy and --output are required");
  }
  return result;
}

std::string preflop_action_id(const gtosd::HuPreflopTree &tree,
                              const gtosd::HuPreflopNode &node,
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
    const auto target_units =
        node.state.committed_this_street[node.state.player_to_act].units() + action.amount.units();
    const auto ante_units = tree.config.ante.units();
    if (ante_units <= 0 || target_units <= 0 || (target_units * 2) % ante_units != 0) {
      throw std::runtime_error("invalid preflop raise target");
    }
    const auto half_antes = (target_units * 2) / ante_units;
    return half_antes % 2 == 0 ? "raise_" + std::to_string(half_antes / 2)
                              : "raise_" + std::to_string(half_antes / 2) + "_5";
  }
  }
  throw std::runtime_error("unknown preflop action");
}

gtosd::HuPreflopBlueprint blueprint_from_candidate(const gtosd::HuPreflopTree &tree,
                                                    const Json &candidate) {
  if (!candidate.contains("preflop_nodes") || candidate.at("tree_fingerprint") != tree.fingerprint) {
    throw std::runtime_error("candidate does not contain a matching full preflop chart");
  }
  gtosd::HuPreflopBlueprint result;
  result.tree_fingerprint = tree.fingerprint;
  result.algorithm = candidate.at("algorithm").get<std::string>();
  result.iterations = candidate.at("iterations").get<std::uint64_t>();
  const auto &charts = candidate.at("preflop_nodes");
  for (const auto &node : tree.nodes) {
    if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
      continue;
    }
    const auto chart_id = node.id == tree.root ? std::string{"CO"}
                                               : "solver_node_" + std::to_string(node.id);
    const auto &chart = charts.at(chart_id);
    gtosd::HuPreflopBlueprintDecision decision;
    decision.node_id = node.id;
    decision.player = node.state.player_to_act;
    decision.action_count = static_cast<std::uint8_t>(node.edges.size());
    for (std::size_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
      const auto &row = chart.at("strategy").at(
          gtosd::class_name(static_cast<gtosd::HandClassId>(hand)));
      for (std::size_t action = 0U; action < node.edges.size(); ++action) {
        decision.strategy[hand][action] =
            row.at(preflop_action_id(tree, node, node.edges[action].action)).get<double>();
      }
    }
    result.decisions.push_back(std::move(decision));
  }
  result.fingerprint = gtosd::fingerprint_hu_preflop_blueprint(result);
  if (!gtosd::validate_hu_preflop_blueprint(tree, result)) {
    throw std::runtime_error("candidate preflop blueprint is invalid");
  }
  return result;
}

double seconds_since(const Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool same_recomposed_values(const gtosd::PostflopRootCounterfactualValues &first,
                            const gtosd::PostflopRootCounterfactualValues &second) {
  constexpr double tolerance = 1.0e-10;
  return std::abs(first.recomposed_value_antes[0] - second.recomposed_value_antes[0]) <=
             tolerance &&
         std::abs(first.recomposed_value_antes[1] - second.recomposed_value_antes[1]) <=
             tolerance;
}

void write_atomic(const std::filesystem::path &path, const std::string &payload) {
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      throw std::runtime_error("cannot open probe output temporary file");
    }
    stream << payload << '\n';
    stream.flush();
    if (!stream) {
      throw std::runtime_error("cannot write probe output temporary file");
    }
  }
  std::error_code error;
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    throw std::runtime_error("cannot atomically replace probe output");
  }
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto arguments = parse_arguments(argc, argv);
    const auto total_started = Clock::now();
    const auto config = gtosd::deserialize_hu_preflop_config_json(read_file(arguments.config));
    if (!config) {
      throw std::runtime_error("invalid HU preflop config");
    }
    const auto tree = gtosd::build_hu_preflop_tree(config.value());
    if (!tree) {
      throw std::runtime_error("cannot build HU preflop tree");
    }
    const auto candidate = Json::parse(read_file(arguments.candidate));
    const auto blueprint = blueprint_from_candidate(tree.value(), candidate);
    const auto decomposition =
        gtosd::derive_hu_preflop_decomposition_plan(tree.value(), blueprint);
    const auto work = decomposition
                          ? gtosd::estimate_hu_preflop_river_work(tree.value(),
                                                                 decomposition.value())
                          : gtosd::Result<gtosd::HuPreflopRiverWorkEstimate,
                                          gtosd::HuPreflopError>::failure(
                                gtosd::HuPreflopError::InvalidConfiguration);
    const auto catalog = work ? gtosd::build_hu_preflop_river_root_catalog(
                                    tree.value(), decomposition.value(), work.value())
                              : gtosd::Result<gtosd::HuPreflopRiverRootCatalog,
                                              gtosd::HuPreflopError>::failure(
                                    gtosd::HuPreflopError::InvalidConfiguration);
    constexpr std::uint64_t batch_payload = 64ULL * 1'024ULL * 1'024ULL;
    const auto batches = catalog ? gtosd::derive_hu_preflop_river_batch_plan(
                                       tree.value(), decomposition.value(), work.value(),
                                       catalog.value(), batch_payload)
                                 : gtosd::Result<gtosd::HuPreflopRiverBatchPlan,
                                                 gtosd::HuPreflopError>::failure(
                                       gtosd::HuPreflopError::InvalidConfiguration);
    if (!decomposition || !work || !catalog || !batches || catalog.value().task_spans.empty()) {
      throw std::runtime_error("cannot build whole-game River catalog");
    }
    const auto setup_seconds = seconds_since(total_started);

    const auto load_started = Clock::now();
    auto loaded =
        gtosd::load_hu_preflop_sampled_postflop_policy(tree.value(), arguments.policy);
    if (!loaded || loaded.value().iterations != blueprint.iterations) {
      throw std::runtime_error("invalid or iteration-mismatched postflop policy");
    }
    const auto policy_entries = loaded.value().entries.size();
    const auto load_seconds = seconds_since(load_started);
    const auto validation_started = Clock::now();
    auto strict = gtosd::make_hu_preflop_validated_sampled_postflop_policy(
        tree.value(), std::move(loaded.value()),
        gtosd::HuPreflopSampledPolicyLookupMode::RequireTrainedInfoset);
    if (!strict) {
      throw std::runtime_error("postflop policy validation failed");
    }
    const auto validation_seconds = seconds_since(validation_started);
    const auto reuse_census = gtosd::analyze_hu_preflop_sampled_policy_reuse(strict.value());
    if (!reuse_census || reuse_census.value().trained_information_sets != policy_entries) {
      throw std::runtime_error("cannot census postflop policy reuse");
    }

    const auto &task = catalog.value().task_spans.front();
    const auto shape_begin = catalog.value().river_shapes.begin() +
                             static_cast<std::ptrdiff_t>(task.first_shape_index);
    const auto shape_end = shape_begin + static_cast<std::ptrdiff_t>(task.shape_count);
    const auto shallow_shape =
        std::ranges::min_element(shape_begin, shape_end, {}, [](const auto &shape) {
          return shape.state.remaining_stacks[0].units();
        });
    if (shallow_shape == shape_end) {
      throw std::runtime_error("cannot select River probe root");
    }
    const auto shape_offset =
        static_cast<std::uint64_t>(std::distance(shape_begin, shallow_shape));
    const auto root_ordinal = task.first_resolver_root + shape_offset * task.board_count * 2U;
    const auto root = gtosd::hu_preflop_river_resolver_root_at(catalog.value(), batches.value(),
                                                               root_ordinal);
    if (!root) {
      throw std::runtime_error("cannot materialize River probe root");
    }

    const auto strict_started = Clock::now();
    const auto strict_profile = gtosd::evaluate_hu_preflop_sampled_policy_river_root_vectorized(
        tree.value(), decomposition.value(), root.value(), strict.value(),
        gtosd::PostflopRootValueMode::AverageStrategy);
    const auto strict_seconds = seconds_since(strict_started);

    auto permissive = strict.value();
    permissive.lookup_mode = gtosd::HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback;
    const auto pair_started = Clock::now();
    const auto pair =
        gtosd::evaluate_hu_preflop_sampled_policy_river_root_vectorized_pair(
            tree.value(), decomposition.value(), root.value(), permissive);
    const auto profile_and_best_response_seconds = seconds_since(pair_started);
    if (!pair) {
      throw std::runtime_error(std::string{"permissive River evaluation failed: "} +
                               gtosd::hu_preflop_error_name(pair.error()));
    }
    const auto &profile = pair.value().profile;
    const auto &best_response = pair.value().best_response;
    const std::array deviation_gains{
        best_response.recomposed_value_antes[0] - profile.recomposed_value_antes[0],
        best_response.recomposed_value_antes[1] - profile.recomposed_value_antes[1]};
    const auto local_nashconv = deviation_gains[0] + deviation_gains[1];
    if (!std::isfinite(local_nashconv) || deviation_gains[0] < -1.0e-10 ||
        deviation_gains[1] < -1.0e-10) {
      throw std::runtime_error("invalid River best-response gain");
    }

    const auto board_materialization_started = Clock::now();
    auto board_roots = gtosd::enumerate_hu_preflop_river_board_roots(
        catalog.value(), batches.value(), 0U, 0U, 0U);
    const auto board_materialization_seconds = seconds_since(board_materialization_started);
    if (!board_roots || board_roots.value().size() != task.shape_count) {
      throw std::runtime_error("cannot materialize board-batch River roots");
    }
    const auto board_batch_started = Clock::now();
    const auto board_batch =
        gtosd::evaluate_hu_preflop_sampled_policy_river_board_batch_vectorized_pair(
            tree.value(), decomposition.value(), board_roots.value(), permissive);
    const auto board_batch_seconds = seconds_since(board_batch_started);
    if (!board_batch || board_batch.value().roots.size() != board_roots.value().size() ||
        shape_offset >= board_batch.value().roots.size() ||
        !same_recomposed_values(
            profile, board_batch.value().roots[static_cast<std::size_t>(shape_offset)].profile) ||
        !same_recomposed_values(
            best_response,
            board_batch.value().roots[static_cast<std::size_t>(shape_offset)].best_response)) {
      throw std::runtime_error("board-batched River evaluation failed differential validation");
    }

    const auto canonical_public_subgames = catalog.value().resolver_roots / 2U;
    const auto measured_seconds_per_subgame = profile_and_best_response_seconds;
    const auto board_batch_seconds_per_subgame =
        board_batch_seconds / static_cast<double>(board_roots.value().size());
    constexpr double seconds_per_year = 365.25 * 24.0 * 60.0 * 60.0;
    const auto projected_serial_seconds =
        static_cast<double>(canonical_public_subgames) * measured_seconds_per_subgame;
    const auto board_batched_projected_serial_seconds =
        static_cast<double>(canonical_public_subgames) * board_batch_seconds_per_subgame;
    const auto trained_rows_per_context =
        reuse_census.value().trained_decision_contexts > 0U
            ? static_cast<double>(reuse_census.value().trained_information_sets) /
                  static_cast<double>(reuse_census.value().trained_decision_contexts)
            : 0.0;
    const bool naive_exact_within_budget =
        projected_serial_seconds <= arguments.maximum_exact_seconds;
    const bool board_batched_exact_within_budget =
        board_batched_projected_serial_seconds <= arguments.maximum_exact_seconds;
    const bool strict_complete = strict_profile.has_value();
    const Json result{
        {"schema", "gtosd.hu_preflop_nashconv_probe.v3"},
        {"status", strict_complete ? "STRICT_COVERAGE_PASS" : "STRICT_COVERAGE_INCOMPLETE"},
        {"nashconv_certified", false},
        {"tree_fingerprint", tree.value().fingerprint},
        {"blueprint_fingerprint", blueprint.fingerprint},
        {"policy_fingerprint", strict.value().policy_fingerprint},
        {"policy_iterations", strict.value().iterations},
        {"policy_entries", policy_entries},
        {"policy_reuse_census",
         {{"scope", "trained_rows_only_not_physical_terminal_work"},
          {"trained_information_sets", reuse_census.value().trained_information_sets},
          {"trained_decision_contexts", reuse_census.value().trained_decision_contexts},
          {"trained_information_sets_by_street",
           reuse_census.value().trained_information_sets_by_street},
          {"trained_information_sets_by_street_player",
           reuse_census.value().trained_information_sets_by_street_player},
          {"trained_decision_contexts_by_street",
           reuse_census.value().trained_decision_contexts_by_street},
          {"trained_decision_contexts_by_street_player",
           reuse_census.value().trained_decision_contexts_by_street_player},
          {"distinct_public_histories_by_street",
           reuse_census.value().distinct_public_histories_by_street},
          {"minimum_information_sets_per_context",
           reuse_census.value().minimum_information_sets_per_context},
          {"maximum_information_sets_per_context",
           reuse_census.value().maximum_information_sets_per_context},
          {"mean_information_sets_per_context",
           reuse_census.value().mean_information_sets_per_context},
          {"trained_rows_per_context", trained_rows_per_context}}},
        {"catalog",
         {{"tasks", catalog.value().task_spans.size()},
          {"canonical_public_subgames", canonical_public_subgames},
          {"resolver_sides", catalog.value().resolver_roots},
          {"canonical_boards", catalog.value().canonical_boards.size()},
          {"river_shapes", catalog.value().river_shapes.size()}}},
        {"probe_root",
         {{"selection", "minimum_remaining_stack_first_task"},
          {"ordinal", root_ordinal},
          {"entry_node", root.value().entry_node},
          {"remaining_stack_units", root.value().state.remaining_stacks[0].units()},
          {"pot_units", root.value().state.pot.units()},
          {"public_history_actions", root.value().action_history.size()}}},
        {"strict_lookup",
         {{"complete", strict_complete},
          {"error", strict_complete ? "none" : gtosd::hu_preflop_error_name(strict_profile.error())},
          {"seconds", strict_seconds}}},
        {"permissive_fallback_diagnostic",
         {{"exact_for_selected_subgame", true},
          {"whole_game_certified", false},
          {"fallback", "uniform_declared_by_policy_contract"},
          {"profile_ev_antes", profile.recomposed_value_antes},
          {"best_response_ev_antes", best_response.recomposed_value_antes},
          {"deviation_gain_antes", deviation_gains},
          {"river_subgame_nashconv_antes", local_nashconv},
          {"profile_and_best_response_seconds", profile_and_best_response_seconds},
          {"maximum_recomposition_error_antes",
           std::max(profile.maximum_recomposition_error_antes,
                    best_response.maximum_recomposition_error_antes)}}},
        {"timing",
         {{"river_evaluator", "vectorized_combo_matrix_board_batch_v2"},
          {"setup_seconds", setup_seconds},
          {"policy_load_seconds", load_seconds},
          {"one_time_policy_validation_seconds", validation_seconds},
          {"measured_seconds_per_public_subgame", measured_seconds_per_subgame}}},
        {"naive_full_river_projection",
         {{"serial_seconds", projected_serial_seconds},
          {"serial_years", projected_serial_seconds / seconds_per_year},
          {"ideal_eight_worker_years", projected_serial_seconds / (8.0 * seconds_per_year)},
          {"scope", "river_only_excludes_upper_street_and_preflop_terminal_reduction"}}},
        {"board_batch_benchmark",
         {{"task_span_index", 0U},
          {"entry_node", task.entry_node},
          {"canonical_board_offset", 0U},
          {"public_history_roots", board_roots.value().size()},
          {"shared_preparation",
           "live_combos_showdown_values_and_private_abstraction_keys"},
          {"root_materialization_seconds", board_materialization_seconds},
          {"seconds", board_batch_seconds},
          {"seconds_per_public_subgame", board_batch_seconds_per_subgame},
          {"selected_root_differential_tolerance_antes", 1.0e-10},
          {"selected_root_differential_pass", true}}},
        {"board_batched_full_river_projection",
         {{"serial_seconds", board_batched_projected_serial_seconds},
          {"serial_years", board_batched_projected_serial_seconds / seconds_per_year},
          {"ideal_eight_worker_years",
           board_batched_projected_serial_seconds / (8.0 * seconds_per_year)},
          {"projection_method",
           "first_entry_first_canonical_board_all_river_shapes_extrapolated"},
          {"scope", "river_only_excludes_upper_street_and_preflop_terminal_reduction"}}},
        {"exact_execution_gate",
         {{"maximum_exact_seconds", arguments.maximum_exact_seconds},
          {"naive_root_by_root_within_budget", naive_exact_within_budget},
          {"board_batched_within_budget", board_batched_exact_within_budget},
          {"status", board_batched_exact_within_budget
                         ? "ELIGIBLE_FOR_EXACT_BOARD_BATCHED_EXECUTION"
                         : "INFEASIBLE_EXACT_BOARD_BATCHED"},
          {"next_required_executor",
           board_batched_exact_within_budget
               ? "checkpointed_board_batch_executor"
               : "cross_board_policy_prefix_trie_or_estimated_only"}}},
        {"total_probe_seconds", seconds_since(total_started)},
        {"decision",
         strict_complete
             ? "measure additional strata before selecting the whole-game executor"
             : "whole-game certification must include the declared uniform fallback or require a "
               "dense policy export; the measured board-batched River path still determines the "
               "exact-execution feasibility gate"},
    };
    write_atomic(arguments.output, result.dump(2));
    std::cout << "HU_PREFLOP_NASHCONV_PROBE="
              << (strict_complete ? "STRICT_COVERAGE_PASS" : "STRICT_COVERAGE_INCOMPLETE")
              << " strict=" << (strict_complete ? "complete" : "missing_infoset")
              << " local_permissive_nashconv=" << local_nashconv
              << " root_by_root_serial_years=" << projected_serial_seconds / seconds_per_year
              << " board_batched_serial_years="
              << board_batched_projected_serial_seconds / seconds_per_year << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HU_PREFLOP_NASHCONV_PROBE=FAIL reason=" << error.what() << '\n';
    return 1;
  }
}
