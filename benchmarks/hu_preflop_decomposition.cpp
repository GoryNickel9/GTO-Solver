#include "gtosd/postflop/canonical_layout.hpp"
#include "gtosd/preflop/hu_preflop_legacy.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

int main(const int argc, char **argv) {
  std::string file_backed_probe;
  std::string backing_file;
  bool build_full_all_in_equity_table = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--file-backed-probe" && index + 1 < argc) {
      file_backed_probe = argv[++index];
    } else if (argument == "--backing-file" && index + 1 < argc) {
      backing_file = argv[++index];
    } else if (argument == "--all-in-equity-table-full") {
      build_full_all_in_equity_table = true;
    } else {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=invalid_argument\n";
      return 64;
    }
  }
  if ((!file_backed_probe.empty() && file_backed_probe != "turn" && file_backed_probe != "flop") ||
      (file_backed_probe.empty() != backing_file.empty()) ||
      (build_full_all_in_equity_table && !file_backed_probe.empty())) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=invalid_probe_configuration\n";
    return 64;
  }

  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
  const auto blueprint =
      tree ? gtosd::make_uniform_hu_preflop_blueprint(tree.value())
           : gtosd::Result<gtosd::HuPreflopBlueprint, gtosd::HuPreflopError>::failure(
                 gtosd::HuPreflopError::GameFailure);
  const auto plan =
      blueprint ? gtosd::derive_hu_preflop_decomposition_plan(tree.value(), blueprint.value())
                : gtosd::Result<gtosd::HuPreflopDecompositionPlan, gtosd::HuPreflopError>::failure(
                      gtosd::HuPreflopError::InvalidConfiguration);
  if (!plan) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason="
              << gtosd::hu_preflop_error_name(plan.error()) << '\n';
    return 1;
  }

  if (build_full_all_in_equity_table) {
    const auto started = std::chrono::steady_clock::now();
    const auto catalog = gtosd::build_hu_preflop_all_in_board_catalog();
    if (!catalog) {
      std::cerr << "HU_PREFLOP_ALL_IN_EQUITY=FAIL reason=setup\n";
      return 9;
    }
    const auto table = gtosd::build_hu_preflop_all_in_equity_table(catalog.value());
    if (!table) {
      std::cerr << "HU_PREFLOP_ALL_IN_EQUITY=FAIL reason=finalize\n";
      return 11;
    }
    const auto table_completed = std::chrono::steady_clock::now();
    std::uint64_t exact_terminal_evaluations = 0U;
    for (const auto &node : tree.value().nodes) {
      if (node.kind != gtosd::HuPreflopNodeKind::TerminalAllIn) {
        continue;
      }
      for (std::uint8_t responding_player = 0U; responding_player < 2U; ++responding_player) {
        const auto evaluated = gtosd::evaluate_hu_preflop_best_response_all_in_terminal(
            tree.value(), blueprint.value(), catalog.value(), table.value(), node.id,
            "benchmark_exact_all_in_profile_v1", responding_player, 1U);
        if (!evaluated) {
          std::cerr << "HU_PREFLOP_ALL_IN_EQUITY=FAIL reason=terminal_" << node.id << "_player_"
                    << static_cast<unsigned>(responding_player) << '\n';
          return 12;
        }
        ++exact_terminal_evaluations;
      }
    }
    const auto finished = std::chrono::steady_clock::now();
    const auto table_seconds = std::chrono::duration<double>(table_completed - started).count();
    const auto terminal_seconds = std::chrono::duration<double>(finished - table_completed).count();
    const auto seconds = std::chrono::duration<double>(finished - started).count();
    const auto table_payload_bytes =
        table.value().matchups.size() * sizeof(gtosd::HuPreflopAllInMatchupCount);
    std::cout << "HU_PREFLOP_ALL_IN_EQUITY=PASS"
              << " canonical_boards=" << catalog.value().boards.size()
              << " physical_unordered_boards=" << catalog.value().physical_unordered_boards
              << " matchup_outcomes=" << table.value().matchup_outcome_count
              << " table_payload_bytes=" << table_payload_bytes
              << " exact_terminal_evaluations=" << exact_terminal_evaluations
              << " table_seconds=" << table_seconds << " terminal_seconds=" << terminal_seconds
              << " elapsed_seconds=" << seconds << " canonical_boards_per_second="
              << static_cast<double>(catalog.value().boards.size()) / table_seconds
              << " table_fingerprint=" << table.value().fingerprint << '\n';
    return 0;
  }

  const auto selected =
      std::ranges::min_element(plan.value().entries, [&](const auto &first, const auto &second) {
        return tree.value().nodes[first.entry_node].state.remaining_stacks[0] <
               tree.value().nodes[second.entry_node].state.remaining_stacks[0];
      });
  const std::array flop{gtosd::parse_card("6c").value(), gtosd::parse_card("7d").value(),
                        gtosd::parse_card("Ah").value()};
  const auto conditioned =
      gtosd::condition_hu_preflop_ranges_on_flop(plan.value(), selected->entry_node, flop);
  const auto quantized =
      conditioned ? gtosd::quantize_hu_preflop_conditioned_ranges(conditioned.value())
                  : gtosd::Result<gtosd::HuPreflopQuantizedRanges, gtosd::HuPreflopError>::failure(
                        gtosd::HuPreflopError::InvalidConfiguration);
  const auto config =
      quantized ? gtosd::make_hu_preflop_postflop_config(tree.value(), selected->entry_node, flop)
                : gtosd::Result<gtosd::PostflopTreeConfig, gtosd::HuPreflopError>::failure(
                      gtosd::HuPreflopError::InvalidConfiguration);
  if (!conditioned || !quantized || !config) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=conditioned_root\n";
    return 1;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto estimate = gtosd::estimate_postflop_layout(config.value(), quantized.value().ranges);
  const auto elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (!estimate) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=postflop_layout\n";
    return 2;
  }
  gtosd::CanonicalLayoutOptions memory_options;
  memory_options.worker_threads = {8U};
  memory_options.state_bytes_per_action = {4U};
  std::uint64_t maximum_estimated_peak_bytes = 0U;
  std::uint64_t maximum_solver_state_bytes = 0U;
  std::uint64_t maximum_state_auxiliary_bytes = 0U;
  std::uint64_t maximum_action_entries = 0U;
  std::uint64_t maximum_physical_public_nodes = 0U;
  std::uint64_t maximum_canonical_public_nodes = 0U;
  std::array<std::uint64_t, 3> maximum_entry_actions_by_street{};
  std::array<std::uint64_t, 3> maximum_entry_infosets_by_street{};
  std::array<std::uint64_t, 3> maximum_entry_decisions_by_street{};
  std::uint32_t maximum_entry_node = 0U;
  for (const auto &candidate : plan.value().entries) {
    const auto candidate_conditioned =
        gtosd::condition_hu_preflop_ranges_on_flop(plan.value(), candidate.entry_node, flop);
    const auto candidate_ranges =
        candidate_conditioned
            ? gtosd::quantize_hu_preflop_conditioned_ranges(candidate_conditioned.value())
            : gtosd::Result<gtosd::HuPreflopQuantizedRanges, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::InvalidConfiguration);
    const auto candidate_config =
        candidate_ranges
            ? gtosd::make_hu_preflop_postflop_config(tree.value(), candidate.entry_node, flop)
            : gtosd::Result<gtosd::PostflopTreeConfig, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::InvalidConfiguration);
    const auto memory =
        candidate_config
            ? gtosd::estimate_canonical_chance_layout(
                  candidate_config.value(), candidate_ranges.value().ranges, memory_options)
            : gtosd::Result<gtosd::CanonicalLayoutReport, gtosd::CanonicalLayoutError>::failure(
                  gtosd::CanonicalLayoutError::InvalidConfiguration);
    if (!memory || memory.value().memory_estimates.size() != 1U) {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=entry_memory_model\n";
      return 3;
    }
    const auto &entry_memory = memory.value().memory_estimates.front();
    const auto &candidate_state = tree.value().nodes[candidate.entry_node].state;
    std::cout << "HU_PREFLOP_DECOMPOSITION_ENTRY"
              << " entry_node=" << candidate.entry_node
              << " pot_units=" << candidate_state.pot.units()
              << " remaining_stack_units=" << candidate_state.remaining_stacks[0].units()
              << " action_entries=" << memory.value().action_entries
              << " production_state_bytes=" << entry_memory.solver_state_bytes
              << " production_estimated_peak_bytes=" << entry_memory.estimated_peak_bytes << '\n';
    if (entry_memory.estimated_peak_bytes > maximum_estimated_peak_bytes) {
      maximum_estimated_peak_bytes = entry_memory.estimated_peak_bytes;
      maximum_solver_state_bytes = entry_memory.solver_state_bytes;
      maximum_state_auxiliary_bytes = entry_memory.state_auxiliary_bytes;
      maximum_action_entries = memory.value().action_entries;
      maximum_physical_public_nodes = memory.value().physical_public_tree.node_count;
      maximum_canonical_public_nodes = memory.value().canonical_public_nodes;
      for (std::size_t street = 0U; street < maximum_entry_actions_by_street.size(); ++street) {
        maximum_entry_actions_by_street[street] = memory.value().streets[street].action_entries;
        maximum_entry_infosets_by_street[street] = memory.value().streets[street].information_sets;
        maximum_entry_decisions_by_street[street] =
            memory.value().streets[street].canonical_decision_nodes;
      }
      maximum_entry_node = candidate.entry_node;
    }
  }
  const auto turn_shapes =
      gtosd::enumerate_hu_postflop_betting_root_shapes(tree.value(), gtosd::Street::Turn);
  const auto river_shapes =
      gtosd::enumerate_hu_postflop_betting_root_shapes(tree.value(), gtosd::Street::River);
  const auto upper_street_terminals =
      gtosd::enumerate_hu_postflop_upper_street_terminal_shapes(tree.value());
  const auto river_work = gtosd::estimate_hu_preflop_river_work(tree.value(), plan.value());
  const auto river_catalog_started = std::chrono::steady_clock::now();
  const auto river_catalog =
      river_work ? gtosd::build_hu_preflop_river_root_catalog(tree.value(), plan.value(),
                                                              river_work.value())
                 : gtosd::Result<gtosd::HuPreflopRiverRootCatalog, gtosd::HuPreflopError>::failure(
                       gtosd::HuPreflopError::InvalidConfiguration);
  const auto river_catalog_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - river_catalog_started)
          .count();
  const auto river_batches =
      river_work && river_catalog
          ? gtosd::derive_hu_preflop_river_batch_plan(tree.value(), plan.value(),
                                                      river_work.value(), river_catalog.value(),
                                                      64ULL * 1'024ULL * 1'024ULL)
          : gtosd::Result<gtosd::HuPreflopRiverBatchPlan, gtosd::HuPreflopError>::failure(
                gtosd::HuPreflopError::InvalidConfiguration);
  const auto first_river_batch =
      river_batches ? gtosd::hu_preflop_river_batch_at(river_batches.value(), 0U)
                    : gtosd::Result<gtosd::HuPreflopRiverBatch, gtosd::HuPreflopError>::failure(
                          gtosd::HuPreflopError::InvalidConfiguration);
  const auto batch_enumeration_started = std::chrono::steady_clock::now();
  const auto first_river_roots =
      river_catalog && river_batches && first_river_batch
          ? gtosd::enumerate_hu_preflop_river_batch_roots(
                river_catalog.value(), river_batches.value(), first_river_batch.value())
          : gtosd::Result<std::vector<gtosd::HuPreflopRiverResolverRoot>, gtosd::HuPreflopError>::
                failure(gtosd::HuPreflopError::InvalidConfiguration);
  const auto batch_enumeration_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - batch_enumeration_started)
          .count();
  if (!turn_shapes || !river_shapes || !upper_street_terminals || !river_work || !river_batches ||
      !river_catalog || !first_river_batch || !first_river_roots) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=nested_boundary_shapes\n";
    return 4;
  }
  std::map<std::pair<std::int64_t, std::int64_t>, gtosd::PublicState> unique_river_states;
  const auto flop_terminal_shapes =
      std::ranges::count_if(upper_street_terminals.value(),
                            [](const auto &shape) { return shape.street == gtosd::Street::Flop; });
  const auto turn_terminal_shapes =
      std::ranges::count_if(upper_street_terminals.value(),
                            [](const auto &shape) { return shape.street == gtosd::Street::Turn; });
  const auto fold_terminal_shapes =
      std::ranges::count_if(upper_street_terminals.value(), [](const auto &shape) {
        return shape.state.status == gtosd::HandStatus::Folded;
      });
  const auto all_in_terminal_shapes =
      std::ranges::count_if(upper_street_terminals.value(), [](const auto &shape) {
        return shape.state.status == gtosd::HandStatus::AllInRunout;
      });
  std::uint64_t maximum_entry_river_shape_count = 0U;
  for (const auto &shape : river_shapes.value()) {
    if (shape.entry_node == maximum_entry_node) {
      ++maximum_entry_river_shape_count;
      unique_river_states.try_emplace(
          std::pair{shape.state.pot.units(), shape.state.remaining_stacks[0].units()}, shape.state);
    }
  }
  std::uint64_t maximum_river_subgame_actions = 0U;
  std::uint64_t maximum_river_subgame_state_bytes = 0U;
  std::uint64_t maximum_river_subgame_peak_bytes = 0U;
  std::optional<gtosd::PostflopTreeConfig> maximum_river_config;
  const auto turn_card = gtosd::parse_card("8h").value();
  const auto river_card = gtosd::parse_card("9s").value();
  for (const auto &[key, state] : unique_river_states) {
    (void)key;
    auto river_config =
        gtosd::make_hu_preflop_postflop_config(tree.value(), maximum_entry_node, flop).value();
    river_config.turn = turn_card;
    river_config.river = river_card;
    river_config.initial_pot = state.pot;
    river_config.effective_stack = state.remaining_stacks[0];
    const auto river_memory = gtosd::estimate_canonical_chance_layout(
        river_config, gtosd::make_uniform_postflop_ranges(), memory_options);
    if (!river_memory || river_memory.value().memory_estimates.size() != 1U) {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=river_memory_model\n";
      return 5;
    }
    const auto &modeled = river_memory.value().memory_estimates.front();
    if (modeled.estimated_peak_bytes > maximum_river_subgame_peak_bytes) {
      maximum_river_subgame_actions = river_memory.value().action_entries;
      maximum_river_subgame_state_bytes = modeled.solver_state_bytes;
      maximum_river_subgame_peak_bytes = modeled.estimated_peak_bytes;
      maximum_river_config = river_config;
    }
  }
  const auto upper_street_state_bytes =
      4U * (maximum_entry_actions_by_street[0] + maximum_entry_actions_by_street[1]) +
      8U * (maximum_entry_decisions_by_street[0] + maximum_entry_decisions_by_street[1]);
  const auto nested_river_peak_lower_bound =
      upper_street_state_bytes + maximum_river_subgame_peak_bytes +
      plan.value().bytes.streaming_certification_live_payload_bytes;
  if (!maximum_river_config) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=missing_river_smoke_config\n";
    return 6;
  }
  gtosd::PostflopProductionSolveRequest solve_request;
  solve_request.iterations = 1U;
  solve_request.enable_detailed_memory_accounting = true;
  const auto solve_options = gtosd::resolve_postflop_production_options(solve_request);
  const auto river_solve =
      solve_options
          ? gtosd::solve_postflop_exact(
                *maximum_river_config, gtosd::make_uniform_postflop_ranges(), solve_options.value())
          : gtosd::Result<gtosd::PostflopSolveResult, gtosd::PostflopSolverError>::failure(
                gtosd::PostflopSolverError::InvalidConfiguration);
  if (!river_solve || river_solve.value().convergence.empty()) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=river_production_smoke\n";
    return 7;
  }
  const auto &river_certification = river_solve.value().convergence.back();
  const auto river_cfvs = gtosd::derive_postflop_root_counterfactual_values(
      *maximum_river_config, gtosd::make_uniform_postflop_ranges(), river_solve.value().checkpoint);
  if (!river_cfvs || river_cfvs.value().players[0].size() != 465U ||
      river_cfvs.value().players[1].size() != 465U ||
      river_cfvs.value().maximum_recomposition_error_antes > 1.0e-4) {
    std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=river_boundary_cfv\n";
    return 8;
  }
  const auto &entry = tree.value().nodes[selected->entry_node];
  std::cout
      << "HU_PREFLOP_DECOMPOSITION=PASS"
      << " plan_fingerprint=" << plan.value().fingerprint
      << " canonical_flops=" << plan.value().canonical_flops
      << " canonical_public_flop_roots=" << plan.value().canonical_public_flop_roots
      << " canonical_flop_boundary_bytes="
      << plan.value().bytes.canonical_both_players_boundary_bytes
      << " physical_flop_boundary_bytes=" << plan.value().bytes.both_players_boundary_bytes
      << " whole_game_coverage_payload_bytes="
      << plan.value().bytes.whole_game_coverage_payload_bytes
      << " whole_game_profile_utility_bytes=" << plan.value().bytes.whole_game_profile_utility_bytes
      << " whole_game_local_continuation_identity_bytes="
      << plan.value().bytes.whole_game_local_continuation_identity_bytes
      << " streaming_certification_live_payload_bytes="
      << plan.value().bytes.streaming_certification_live_payload_bytes
      << " entry_node=" << selected->entry_node << " pot_units=" << entry.state.pot.units()
      << " remaining_stack_units=" << entry.state.remaining_stacks[0].units()
      << " quantization_max_error=" << quantized.value().maximum_absolute_probability_error
      << " layout_seconds=" << elapsed
      << " physical_public_nodes=" << estimate.value().physical_public_tree.node_count
      << " canonical_public_nodes=" << estimate.value().canonical_public_nodes
      << " information_sets=" << estimate.value().information_sets
      << " actions=" << estimate.value().actions
      << " float64_regret_bytes=" << estimate.value().regret_bytes
      << " float64_strategy_bytes=" << estimate.value().strategy_bytes
      << " maximum_entry_node=" << maximum_entry_node
      << " maximum_action_entries=" << maximum_action_entries
      << " maximum_physical_public_nodes=" << maximum_physical_public_nodes
      << " maximum_canonical_public_nodes=" << maximum_canonical_public_nodes
      << " maximum_entry_flop_actions=" << maximum_entry_actions_by_street[0]
      << " maximum_entry_turn_actions=" << maximum_entry_actions_by_street[1]
      << " maximum_entry_river_actions=" << maximum_entry_actions_by_street[2]
      << " maximum_entry_flop_infosets=" << maximum_entry_infosets_by_street[0]
      << " maximum_entry_turn_infosets=" << maximum_entry_infosets_by_street[1]
      << " maximum_entry_river_infosets=" << maximum_entry_infosets_by_street[2]
      << " turn_betting_root_shapes=" << turn_shapes.value().size()
      << " river_betting_root_shapes=" << river_shapes.value().size()
      << " upper_street_terminal_shapes=" << upper_street_terminals.value().size()
      << " flop_terminal_shapes=" << flop_terminal_shapes
      << " turn_terminal_shapes=" << turn_terminal_shapes
      << " fold_terminal_shapes=" << fold_terminal_shapes
      << " all_in_terminal_shapes=" << all_in_terminal_shapes
      << " physical_public_board_histories=" << river_work.value().physical_public_board_histories
      << " canonical_public_board_histories=" << river_work.value().canonical_public_board_histories
      << " minimum_canonical_runouts_per_flop="
      << river_work.value().minimum_canonical_runouts_per_flop
      << " maximum_canonical_runouts_per_flop="
      << river_work.value().maximum_canonical_runouts_per_flop
      << " physical_river_resolver_roots=" << river_work.value().physical_resolver_roots
      << " canonical_river_resolver_roots=" << river_work.value().canonical_resolver_roots
      << " canonical_river_roots_both_players="
      << river_work.value().canonical_roots_for_both_players
      << " canonical_river_public_subgame_solves=" << river_work.value().canonical_resolver_roots
      << " river_boundary_bytes_per_root=" << river_work.value().boundary_bytes_per_resolver_root
      << " fully_materialized_river_boundary_bytes="
      << river_work.value().fully_materialized_boundary_bytes
      << " maximum_br_turn_components_per_river_root="
      << river_work.value().maximum_best_response_turn_components_per_root
      << " br_river_boundary_bytes_per_root="
      << river_work.value().best_response_boundary_bytes_per_resolver_root
      << " fully_materialized_br_river_boundary_bytes="
      << river_work.value().fully_materialized_best_response_boundary_bytes
      << " maximum_canonical_river_roots_per_physical_turn_leaf="
      << river_work.value().maximum_canonical_river_roots_per_physical_turn_leaf
      << " br_river_leaf_accumulator_value_bytes="
      << river_work.value().best_response_leaf_accumulator_value_bytes
      << " maximum_br_river_leaf_manifest_bytes="
      << river_work.value().maximum_best_response_leaf_manifest_bytes
      << " maximum_br_river_leaf_live_payload_bytes="
      << river_work.value().maximum_best_response_leaf_live_payload_bytes
      << " br_flop_task_value_bytes=" << river_work.value().best_response_task_value_bytes
      << " br_entry_accumulator_value_bytes="
      << river_work.value().best_response_entry_accumulator_value_bytes
      << " br_entry_reduction_live_payload_bytes="
      << river_work.value().best_response_entry_reduction_live_payload_bytes
      << " all_in_canonical_complete_boards=" << river_work.value().all_in_canonical_complete_boards
      << " all_in_board_catalog_payload_bytes="
      << river_work.value().all_in_board_catalog_payload_bytes
      << " all_in_matchup_table_payload_bytes="
      << river_work.value().all_in_matchup_table_payload_bytes
      << " all_in_one_board_scratch_payload_bytes="
      << river_work.value().all_in_one_board_scratch_payload_bytes
      << " all_in_streaming_live_payload_bytes="
      << river_work.value().all_in_streaming_live_payload_bytes
      << " river_batch_payload_target_bytes=" << river_batches.value().target_batch_payload_bytes
      << " river_roots_per_batch=" << river_batches.value().roots_per_batch
      << " river_batch_count=" << river_batches.value().batch_count
      << " river_final_batch_roots=" << river_batches.value().final_batch_root_count
      << " river_maximum_batch_payload_bytes=" << river_batches.value().maximum_batch_payload_bytes
      << " river_root_catalog_boards=" << river_catalog.value().canonical_boards.size()
      << " river_root_catalog_shapes=" << river_catalog.value().river_shapes.size()
      << " river_root_catalog_task_spans=" << river_catalog.value().task_spans.size()
      << " river_root_catalog_seconds=" << river_catalog_seconds
      << " first_river_batch_roots=" << first_river_roots.value().size()
      << " first_river_batch_enumeration_seconds=" << batch_enumeration_seconds
      << " maximum_entry_river_shape_count=" << maximum_entry_river_shape_count
      << " maximum_entry_unique_river_state_shapes=" << unique_river_states.size()
      << " maximum_river_subgame_actions=" << maximum_river_subgame_actions
      << " maximum_river_subgame_state_bytes=" << maximum_river_subgame_state_bytes
      << " maximum_river_subgame_peak_bytes=" << maximum_river_subgame_peak_bytes
      << " upper_street_state_bytes=" << upper_street_state_bytes
      << " nested_river_peak_lower_bound=" << nested_river_peak_lower_bound
      << " river_smoke_iterations=" << river_solve.value().checkpoint.completed_iterations
      << " river_smoke_actions=" << river_solve.value().actions
      << " river_smoke_traversal_seconds=" << river_solve.value().timings.traversal_seconds
      << " river_smoke_total_seconds=" << river_solve.value().timings.total_seconds
      << " projected_one_iteration_public_sweep_traversal_seconds="
      << river_solve.value().timings.traversal_seconds *
             static_cast<double>(river_work.value().canonical_resolver_roots)
      << " river_smoke_nashconv_antes=" << river_certification.nash_conv_antes
      << " river_smoke_normalized_nashconv=" << river_certification.normalized_nash_conv
      << " river_smoke_managed_peak_bytes="
      << (river_solve.value().solver_memory_accounting
              ? river_solve.value().solver_memory_accounting->maximum_allocated_bytes
              : 0U)
      << " river_boundary_values_per_player=" << river_cfvs.value().players[0].size()
      << " river_boundary_recomposition_error_antes="
      << river_cfvs.value().maximum_recomposition_error_antes
      << " production_state_bytes=" << maximum_solver_state_bytes
      << " production_state_auxiliary_bytes=" << maximum_state_auxiliary_bytes
      << " production_estimated_peak_bytes=" << maximum_estimated_peak_bytes << std::endl;

  if (!file_backed_probe.empty()) {
    const auto maximum_conditioned =
        gtosd::condition_hu_preflop_ranges_on_flop(plan.value(), maximum_entry_node, flop);
    const auto maximum_ranges =
        maximum_conditioned
            ? gtosd::quantize_hu_preflop_conditioned_ranges(maximum_conditioned.value())
            : gtosd::Result<gtosd::HuPreflopQuantizedRanges, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::InvalidConfiguration);
    auto probe_config =
        maximum_ranges
            ? gtosd::make_hu_preflop_postflop_config(tree.value(), maximum_entry_node, flop)
            : gtosd::Result<gtosd::PostflopTreeConfig, gtosd::HuPreflopError>::failure(
                  gtosd::HuPreflopError::InvalidConfiguration);
    std::uint64_t probe_actions = maximum_action_entries;
    if (!maximum_ranges || !probe_config) {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=probe_root\n";
      return 9;
    }
    if (file_backed_probe == "turn") {
      std::map<std::pair<std::int64_t, std::int64_t>, gtosd::PublicState> unique_turn_states;
      for (const auto &shape : turn_shapes.value()) {
        if (shape.entry_node == maximum_entry_node) {
          unique_turn_states.try_emplace(
              std::pair{shape.state.pot.units(), shape.state.remaining_stacks[0].units()},
              shape.state);
        }
      }
      std::optional<gtosd::PostflopTreeConfig> maximum_turn_config;
      probe_actions = 0U;
      for (const auto &[key, state] : unique_turn_states) {
        (void)key;
        auto candidate = probe_config.value();
        candidate.turn = turn_card;
        candidate.initial_pot = state.pot;
        candidate.effective_stack = state.remaining_stacks[0];
        const auto memory = gtosd::estimate_canonical_chance_layout(
            candidate, maximum_ranges.value().ranges, memory_options);
        if (!memory || memory.value().memory_estimates.size() != 1U) {
          std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=turn_probe_memory\n";
          return 10;
        }
        if (memory.value().action_entries > probe_actions) {
          probe_actions = memory.value().action_entries;
          maximum_turn_config = std::move(candidate);
        }
      }
      if (!maximum_turn_config) {
        std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=turn_probe_root\n";
        return 11;
      }
      probe_config = gtosd::Result<gtosd::PostflopTreeConfig, gtosd::HuPreflopError>::success(
          std::move(*maximum_turn_config));
    }

    const auto absolute_backing = std::filesystem::absolute(backing_file);
    if (std::filesystem::exists(absolute_backing) ||
        !std::filesystem::exists(absolute_backing.parent_path())) {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=unsafe_backing_path\n";
      return 12;
    }
    auto prepared = gtosd::prepare_postflop_tree(probe_config.value(),
                                                 maximum_ranges.value().ranges, true, true, false);
    if (!prepared) {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=probe_layout error="
                << gtosd::postflop_solver_error_name(prepared.error()) << '\n';
      return 13;
    }
    const auto state_bytes = gtosd::prepared_postflop_solver_state_bytes(
        *prepared.value(), gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy);
    if (!state_bytes) {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=probe_state_bytes\n";
      return 14;
    }
    std::cout << "HU_PREFLOP_FILE_BACKED_PROBE_START"
              << " street=" << file_backed_probe << " actions=" << probe_actions
              << " logical_state_bytes=" << state_bytes.value()
              << " current_rss_bytes=" << gtosd::process_current_rss_bytes()
              << " backing_file=" << absolute_backing.string() << std::endl;

    double probe_solver_seconds = 0.0;
    double probe_nashconv = 0.0;
    std::uint64_t probe_managed_peak_bytes = 0U;
    bool backing_present_during_solve = false;
    {
      gtosd::PostflopProductionSolveRequest probe_request;
      probe_request.iterations = 1U;
      probe_request.enable_detailed_memory_accounting = true;
      const auto resolved = gtosd::resolve_postflop_production_options(probe_request);
      if (!resolved) {
        std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=probe_options\n";
        return 15;
      }
      auto probe_options = resolved.value();
      probe_options.resident_working_set_budget_bytes = 64ULL * 1'024ULL * 1'024ULL;
      probe_options.backing_file = absolute_backing.string();
      probe_options.parallel_action_depth = 0U;
      const auto probe_solve = gtosd::solve_postflop_exact(*prepared.value(), probe_options);
      if (!probe_solve || probe_solve.value().convergence.empty() ||
          probe_solve.value().checkpoint.runtime_state == nullptr) {
        std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=file_backed_probe_solve"
                  << (probe_solve ? ""
                                  : std::string{" error="} +
                                        gtosd::postflop_solver_error_name(probe_solve.error()))
                  << '\n';
        return 16;
      }
      backing_present_during_solve = std::filesystem::exists(absolute_backing);
      probe_solver_seconds = probe_solve.value().timings.total_seconds;
      probe_nashconv = probe_solve.value().convergence.back().normalized_nash_conv;
      probe_managed_peak_bytes =
          probe_solve.value().solver_memory_accounting
              ? probe_solve.value().solver_memory_accounting->maximum_allocated_bytes
              : 0U;
    }
    const bool backing_deleted = !std::filesystem::exists(absolute_backing);
    if (!backing_present_during_solve || !backing_deleted) {
      std::cerr << "HU_PREFLOP_DECOMPOSITION=FAIL reason=backing_lifecycle\n";
      return 17;
    }
    std::cout << "HU_PREFLOP_FILE_BACKED_PROBE=PASS"
              << " street=" << file_backed_probe << " actions=" << probe_actions
              << " logical_state_bytes=" << state_bytes.value()
              << " solver_seconds=" << probe_solver_seconds
              << " normalized_nashconv=" << probe_nashconv
              << " managed_peak_bytes=" << probe_managed_peak_bytes
              << " process_peak_rss_bytes=" << gtosd::process_peak_rss_bytes()
              << " backing_deleted=true" << std::endl;
  }
  return 0;
}
