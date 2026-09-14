#include "gtosd/postflop/postflop_subgame.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/solver.hpp"

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
#include <vector>

namespace {

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void require_near(const double actual, const double expected, const double tolerance,
                  const std::string_view message) {
  require(std::isfinite(actual) && std::isfinite(expected) &&
              std::abs(actual - expected) <= tolerance,
          message);
}

gtosd::PostflopTreeConfig
make_river_config(const gtosd::PostflopBenchmark benchmark = gtosd::PostflopBenchmark::PfF1) {
  auto config = gtosd::make_postflop_benchmark_config(benchmark).value();
  std::uint64_t board_mask = 0U;
  for (const auto card : config.flop) {
    board_mask |= card.mask();
  }
  for (std::uint8_t position = 0U; position < 36U && (!config.turn || !config.river); ++position) {
    const auto index = benchmark == gtosd::PostflopBenchmark::PfF1
                           ? position
                           : static_cast<std::uint8_t>(35U - position);
    const auto card = gtosd::CardId::from_index(index).value();
    if ((card.mask() & board_mask) != 0U) {
      continue;
    }
    if (!config.turn) {
      config.turn = card;
    } else {
      config.river = card;
    }
    board_mask |= card.mask();
  }
  const auto half_pot = gtosd::PotPercentage::from_basis_points(5'000).value();
  for (auto &player : config.streets[2].players) {
    for (auto &scenario : player) {
      scenario.aggressive_sizes = {half_pot};
      scenario.raise_depth = 0U;
      scenario.all_in_mode = gtosd::AllInMode::Disabled;
    }
  }
  return config;
}

gtosd::ComboId combo_id(const gtosd::CardId first, const gtosd::CardId second) {
  const auto target = first.mask() | second.mask();
  const auto combos = gtosd::all_combos();
  const auto found = std::ranges::find_if(combos, [&](const auto &combo) {
    return (combo.first.mask() | combo.second.mask()) == target;
  });
  require(found != combos.end(), "physical combo exists");
  return static_cast<gtosd::ComboId>(std::distance(combos.begin(), found));
}

gtosd::PostflopRanges make_sparse_asymmetric_ranges(const gtosd::PostflopTreeConfig &config) {
  gtosd::PostflopRanges ranges;
  const auto board = gtosd::configured_board(config);
  std::uint64_t board_mask = 0U;
  for (const auto card : board) {
    board_mask |= card.mask();
  }
  std::vector<gtosd::CardId> available;
  for (std::uint8_t index = 0U; index < 36U && available.size() < 8U; ++index) {
    const auto card = gtosd::CardId::from_index(index).value();
    if ((card.mask() & board_mask) == 0U) {
      available.push_back(card);
    }
  }
  require(available.size() == 8U, "eight non-board cards are available");
  const std::array<gtosd::ComboId, 3> first{combo_id(available[0], available[1]),
                                            combo_id(available[2], available[3]),
                                            combo_id(available[4], available[5])};
  const std::array<gtosd::ComboId, 3> second{combo_id(available[0], available[2]),
                                             combo_id(available[4], available[6]),
                                             combo_id(available[1], available[7])};
  const std::array<std::uint16_t, 3> first_weights{10'000U, 7'500U, 5'000U};
  const std::array<std::uint16_t, 3> second_weights{9'000U, 6'000U, 3'000U};
  for (std::size_t index = 0U; index < first.size(); ++index) {
    ranges.players[0][first[index]] =
        gtosd::RangeWeight::from_basis_points(first_weights[index]).value();
    ranges.players[1][second[index]] =
        gtosd::RangeWeight::from_basis_points(second_weights[index]).value();
  }
  require(gtosd::validate_postflop_ranges(config, ranges).has_value(),
          "sparse asymmetric ranges are valid");
  return ranges;
}

gtosd::PostflopRanges make_made_hand_collision_ranges(const gtosd::PostflopTreeConfig &config) {
  const auto seven_diamonds = gtosd::parse_card("7d").value();
  const auto seven_hearts = gtosd::parse_card("7h").value();
  const auto seven_spades = gtosd::parse_card("7s").value();
  const auto eight_clubs = gtosd::parse_card("8c").value();
  const auto eight_diamonds = gtosd::parse_card("8d").value();
  const auto eight_hearts = gtosd::parse_card("8h").value();
  const std::array<gtosd::ComboId, 3> first{combo_id(seven_diamonds, seven_hearts),
                                            combo_id(seven_diamonds, seven_spades),
                                            combo_id(seven_hearts, seven_spades)};
  const std::array<gtosd::ComboId, 3> second{combo_id(eight_clubs, eight_diamonds),
                                             combo_id(eight_clubs, eight_hearts),
                                             combo_id(eight_diamonds, eight_hearts)};
  const std::array<std::uint16_t, 3> first_weights{10'000U, 8'000U, 6'000U};
  const std::array<std::uint16_t, 3> second_weights{9'000U, 7'000U, 5'000U};
  gtosd::PostflopRanges ranges;
  for (std::size_t index = 0U; index < first.size(); ++index) {
    ranges.players[0][first[index]] =
        gtosd::RangeWeight::from_basis_points(first_weights[index]).value();
    ranges.players[1][second[index]] =
        gtosd::RangeWeight::from_basis_points(second_weights[index]).value();
  }
  require(gtosd::validate_postflop_ranges(config, ranges).has_value(),
          "made-hand collision ranges are valid");
  return ranges;
}

gtosd::PostflopRanges
make_blocker_sensitive_collision_ranges(const gtosd::PostflopTreeConfig &config) {
  const auto seven_diamonds = gtosd::parse_card("7d").value();
  const auto seven_hearts = gtosd::parse_card("7h").value();
  const auto seven_spades = gtosd::parse_card("7s").value();
  const auto eight_clubs = gtosd::parse_card("8c").value();
  const auto eight_diamonds = gtosd::parse_card("8d").value();
  const auto eight_hearts = gtosd::parse_card("8h").value();
  const std::array<gtosd::ComboId, 3> first{combo_id(seven_diamonds, seven_hearts),
                                            combo_id(seven_diamonds, seven_spades),
                                            combo_id(seven_hearts, seven_spades)};
  const std::array<gtosd::ComboId, 3> second{combo_id(seven_diamonds, eight_clubs),
                                             combo_id(seven_hearts, eight_diamonds),
                                             combo_id(seven_spades, eight_hearts)};
  gtosd::PostflopRanges ranges;
  for (std::size_t index = 0U; index < first.size(); ++index) {
    ranges.players[0][first[index]] =
        gtosd::RangeWeight::from_basis_points(10'000U - 2'000U * index).value();
    ranges.players[1][second[index]] =
        gtosd::RangeWeight::from_basis_points(9'000U - 2'000U * index).value();
  }
  require(gtosd::validate_postflop_ranges(config, ranges).has_value(),
          "blocker-sensitive collision ranges are valid");
  return ranges;
}

gtosd::PostflopRanges make_medium_weighted_ranges(const gtosd::PostflopTreeConfig &config) {
  constexpr std::size_t combos_per_player = 96U;
  const auto combos = gtosd::all_combos();
  std::uint64_t board_mask = 0U;
  for (const auto card : gtosd::configured_board(config)) {
    board_mask |= card.mask();
  }
  gtosd::PostflopRanges ranges;
  std::size_t first_count = 0U;
  std::size_t second_count = 0U;
  for (std::size_t offset = 0U; offset < combos.size() && (first_count < combos_per_player ||
                                                           second_count < combos_per_player);
       ++offset) {
    const auto first_id = static_cast<gtosd::ComboId>(offset);
    const auto second_id = static_cast<gtosd::ComboId>(combos.size() - 1U - offset);
    const auto first_mask = combos[first_id].first.mask() | combos[first_id].second.mask();
    const auto second_mask = combos[second_id].first.mask() | combos[second_id].second.mask();
    if (first_count < combos_per_player && (first_mask & board_mask) == 0U) {
      const auto weight = static_cast<std::uint16_t>(
          2'500U + 2'500U * static_cast<std::uint16_t>((first_id * 17U) % 4U));
      ranges.players[0][first_id] = gtosd::RangeWeight::from_basis_points(weight).value();
      ++first_count;
    }
    if (second_count < combos_per_player && (second_mask & board_mask) == 0U) {
      const auto weight = static_cast<std::uint16_t>(
          2'500U + 2'500U * static_cast<std::uint16_t>((second_id * 13U + 1U) % 4U));
      ranges.players[1][second_id] = gtosd::RangeWeight::from_basis_points(weight).value();
      ++second_count;
    }
  }
  require(first_count == combos_per_player && second_count == combos_per_player,
          "medium weighted fixture selects both physical ranges");
  require(gtosd::validate_postflop_ranges(config, ranges).has_value(),
          "medium weighted ranges are valid");
  return ranges;
}

gtosd::SolverConfig production_config(const std::uint64_t iterations) {
  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::ProductionDcfr;
  config.iterations = iterations;
  config.dcfr = {1.5, 0.0, 3.0};
  return config;
}

void test_postflop_projection_and_safe_resolving() {
  const auto config = make_river_config();
  const auto ranges = make_sparse_asymmetric_ranges(config);
  gtosd::PostflopProductionSolveRequest request;
  request.iterations = 256U;
  const auto options = gtosd::resolve_postflop_production_options(request);
  require(options.has_value(), "production postflop options resolve");
  const auto blueprint_started = std::chrono::steady_clock::now();
  const auto solved = gtosd::solve_postflop_exact(config, ranges, options.value());
  const double blueprint_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - blueprint_started).count();
  require(solved.has_value() && !solved.value().convergence.empty(),
          "exact ProductionDcfr blueprint solves");

  const auto projection = gtosd::project_fixed_river_postflop_game(
      config, ranges, solved.value().checkpoint, {64U, 10'000U});
  require(projection.has_value() && projection.value().physical_deals == 5U,
          "projection keeps every compatible weighted physical deal");
  require(projection.value().byte_model.total_bytes > 0U &&
              !projection.value().byte_model.allocator_overhead_included,
          "projection publishes a bounded byte model without hiding allocator scope");
  const auto projected_certification =
      gtosd::calculate_nash_conv(projection.value().game, projection.value().blueprint);
  require(projected_certification.has_value(), "projected exact blueprint certifies globally");
  const auto &postflop_certification = solved.value().convergence.back();
  require(std::abs(projected_certification.value().profile_value[0] -
                   postflop_certification.profile_value_antes[0]) <= 1.0e-9 &&
              std::abs(projected_certification.value().profile_value[1] -
                       postflop_certification.profile_value_antes[1]) <= 1.0e-9 &&
              std::abs(projected_certification.value().normalized_nash_conv -
                       postflop_certification.normalized_nash_conv) <= 1.0e-9,
          "finite projection reproduces the exact postflop profile and BR oracle");

  const auto root = std::ranges::find_if(projection.value().public_states, [&](const auto &state) {
    return state.source_public_node == projection.value().source_root;
  });
  require(root != projection.value().public_states.end(), "projected public root is indexed");
  const auto checked = std::ranges::find_if(
      root->actions, [](const auto &action) { return action.type == gtosd::ActionType::Check; });
  require(checked != root->actions.end(), "root check action identifies the river subgame");
  const auto checked_index =
      static_cast<std::size_t>(std::distance(root->actions.begin(), checked));
  const auto subgame_node = root->child_public_nodes[checked_index];
  const auto definition =
      gtosd::define_projected_postflop_subgame(projection.value(), subgame_node, 0U);
  require(definition.has_value() &&
              gtosd::validate_public_subgame(projection.value().game, definition.value()),
          "postflop public state is closed over all private deal occurrences");

  const auto boundary_started = std::chrono::steady_clock::now();
  const auto boundary = gtosd::derive_subgame_boundary(projection.value().game, definition.value(),
                                                       projection.value().blueprint,
                                                       projection.value().source_iterations);
  const double boundary_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - boundary_started).count();
  require(boundary.has_value(), "postflop boundary derives from the exact blueprint");
  const auto boundary_bytes = gtosd::estimate_subgame_boundary_bytes(
      projection.value().game, definition.value(), boundary.value());
  require(boundary_bytes.has_value() && boundary_bytes.value().minimum_runtime_bytes > 0U,
          "postflop boundary reports runtime and serialized bytes");

  const auto boundary_path =
      std::filesystem::temp_directory_path() / "gtosd-postflop-subgame-boundary.bin";
  std::error_code ignored;
  std::filesystem::remove(boundary_path, ignored);
  require(gtosd::save_subgame_boundary(boundary.value(), boundary_path.string()).has_value(),
          "postflop boundary saves atomically");
  const auto restored = gtosd::load_subgame_boundary(boundary_path.string());
  require(restored.has_value() && restored.value() == boundary.value(),
          "postflop boundary round-trips with checksum");
  std::filesystem::remove(boundary_path, ignored);

  const auto gadget = gtosd::build_safe_resolving_gadget(projection.value().game,
                                                         definition.value(), boundary.value());
  require(gadget.has_value(), "postflop opt-out gadget builds");
  const auto resolve_started = std::chrono::steady_clock::now();
  const auto resolved = gtosd::solve_finite_game(gadget.value(), production_config(20'000U));
  const double resolve_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - resolve_started).count();
  require(resolved.has_value(), "postflop gadget solves with ProductionDcfr");
  const auto spliced = gtosd::splice_resolved_subgame_strategy(
      projection.value().game, definition.value(), boundary.value(), projection.value().blueprint,
      resolved.value().average_strategy);
  require(spliced.has_value(), "postflop resolving-player strategy splices");
  const auto before =
      gtosd::exact_best_response(projection.value().game, projection.value().blueprint, 1U);
  const auto after = gtosd::exact_best_response(projection.value().game, spliced.value(), 1U);
  require(before.has_value() && after.has_value() &&
              after.value().value <= before.value().value + 1.0e-3,
          "postflop safe resolve does not increase the global opponent BR");

  auto incomplete = boundary.value();
  incomplete.values.pop_back();
  require(
      !gtosd::validate_subgame_boundary(projection.value().game, definition.value(), incomplete),
      "postflop incomplete private boundary is rejected");
  require(!gtosd::define_projected_postflop_subgame(projection.value(),
                                                    std::numeric_limits<gtosd::NodeId>::max(), 0U),
          "off-tree postflop public node is rejected");
  auto mismatched = solved.value().checkpoint;
  mismatched.game_fingerprint.push_back('x');
  require(!gtosd::project_fixed_river_postflop_game(config, ranges, mismatched, {64U, 10'000U}),
          "checkpoint from another postflop game is rejected");

  std::cout << "POSTFLOP_SUBGAME fixture=R2S-RIVER-SPARSE-001"
            << " physical_deals=" << projection.value().physical_deals
            << " projected_nodes=" << projection.value().game.nodes.size()
            << " projection_bytes=" << projection.value().byte_model.total_bytes
            << " boundary_bytes=" << boundary_bytes.value().minimum_runtime_bytes
            << " blueprint_seconds=" << blueprint_seconds
            << " projection_seconds=" << projection.value().projection_seconds
            << " boundary_seconds=" << boundary_seconds << " resolve_seconds=" << resolve_seconds
            << " br_before=" << before.value().value << " br_after=" << after.value().value << '\n';
}

void test_bucket_and_subgame_composition_is_measured_in_original_game() {
  const auto config = make_river_config();
  const auto ranges = make_made_hand_collision_ranges(config);
  gtosd::PostflopProductionSolveRequest request;
  request.iterations = 256U;
  const auto options = gtosd::resolve_postflop_production_options(request).value();
  const auto solved = gtosd::solve_postflop_exact(config, ranges, options);
  require(solved.has_value(), "composition exact blueprint solves");
  const auto projection = gtosd::project_fixed_river_postflop_game(
      config, ranges, solved.value().checkpoint, {64U, 10'000U});
  require(projection.has_value(), "composition exact game projects");
  const auto policy = gtosd::make_projected_postflop_card_abstraction(projection.value());
  require(policy.has_value(), "projected made-hand policy builds");
  const auto summary =
      gtosd::validate_card_abstraction_policy(projection.value().game, policy.value());
  require(summary.has_value() &&
              summary.value().abstract_information_sets < summary.value().original_information_sets,
          "projected bucket policy is complete, action-compatible and genuinely compresses");
  const auto abstract_game = gtosd::apply_card_abstraction(projection.value().game, policy.value());
  const auto abstract_blueprint = gtosd::aggregate_strategy_profile(
      projection.value().game, policy.value(), projection.value().blueprint);
  require(abstract_game.has_value() && abstract_blueprint.has_value(),
          "exact blueprint aggregates onto the projected buckets");

  const auto native_started = std::chrono::steady_clock::now();
  const auto native =
      gtosd::build_fixed_river_bucket_game(config, ranges, {64U, 64U, 100'000U, true});
  require(native.has_value(), "bucket-native fixed-river game builds");
  require(native.value().work_model.physical_deals_preprocessed ==
              projection.value().physical_deals,
          "bucket-native preprocessing preserves every compatible physical deal");
  require(native.value().work_model.bucket_pairs == native.value().bucket_pairs.size() &&
              native.value().work_model.bucket_pairs < projection.value().physical_deals,
          "bucket-native chance root has fewer outcomes than the physical projection");
  require(native.value().work_model.bucket_node_instances_per_player_pass <
                  native.value().work_model.physical_node_instances_per_player_pass &&
              native.value().work_model.bucket_action_entries_per_player_pass <
                  native.value().work_model.physical_action_entries_per_player_pass,
          "bucket-native training removes physical deal work from node and action passes");
  require(native.value().byte_model.total_bytes > 0U &&
              native.value().byte_model.finite_game_bytes <
                  projection.value().byte_model.finite_game_bytes &&
              !native.value().byte_model.allocator_overhead_included,
          "bucket-native game reports a smaller bounded byte model");

  require(native.value().validation_game.has_value(),
          "collision fixture materializes the optional validation game");
  const auto native_uniform = gtosd::uniform_strategy_profile(*native.value().validation_game);
  const auto physical_uniform = gtosd::uniform_strategy_profile(abstract_game.value());
  const auto native_uniform_value =
      native_uniform ? gtosd::evaluate_strategy_profile(*native.value().validation_game,
                                                        native_uniform.value())
                     : gtosd::Result<std::array<double, 2>, gtosd::SolverError>::failure(
                           gtosd::SolverError::InvalidStrategy);
  const auto physical_uniform_value =
      physical_uniform
          ? gtosd::evaluate_strategy_profile(abstract_game.value(), physical_uniform.value())
          : gtosd::Result<std::array<double, 2>, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidStrategy);
  require(native_uniform_value.has_value() && physical_uniform_value.has_value(),
          "uniform profiles evaluate in native and physical abstract games");
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    require_near(native_uniform_value.value()[player], physical_uniform_value.value()[player],
                 1.0e-12, "joint bucket mass reproduces physical blocker-weighted uniform EV");
  }

  const auto native_solved =
      gtosd::solve_fixed_river_bucket_game(native.value(), production_config(256U));
  const auto physical_abstract_solved =
      gtosd::solve_finite_game(abstract_game.value(), production_config(256U));
  require(native_solved.has_value() && physical_abstract_solved.has_value(),
          "bucket-native and physical abstract games solve with ProductionDcfr");
  require(native_solved.value().average_strategy.size() ==
              physical_abstract_solved.value().average_strategy.size(),
          "bucket-native and physical abstract games expose the same strategy domain");
  for (const auto &[information_set, native_strategy] : native_solved.value().average_strategy) {
    const auto physical_strategy =
        physical_abstract_solved.value().average_strategy.find(information_set);
    require(physical_strategy != physical_abstract_solved.value().average_strategy.end() &&
                physical_strategy->second.actions == native_strategy.actions &&
                physical_strategy->second.probabilities.size() ==
                    native_strategy.probabilities.size(),
            "bucket-native strategy infoset matches the physical abstract oracle");
    for (std::size_t action = 0U; action < native_strategy.probabilities.size(); ++action) {
      require_near(native_strategy.probabilities[action],
                   physical_strategy->second.probabilities[action], 1.0e-11,
                   "bucket-native ProductionDcfr strategy matches physical abstract traversal");
    }
  }

  const auto lifted_native = gtosd::lift_fixed_river_bucket_strategy(
      projection.value(), native.value(), native_solved.value().average_strategy);
  const auto native_original_certification =
      lifted_native ? gtosd::calculate_nash_conv(projection.value().game, lifted_native.value())
                    : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                          gtosd::SolverError::InvalidStrategy);
  require(native_original_certification.has_value() &&
              std::isfinite(native_original_certification.value().normalized_nash_conv),
          "bucket-native strategy lifts and certifies in the original physical game");

  const auto &decision = native.value().public_decisions.front();
  const auto &query_bucket = native.value().player_buckets[decision.player_to_act].front();
  require(query_bucket.members.size() >= 2U,
          "collision fixture exposes multiple physical combos in one queried bucket");
  const auto first_query = gtosd::query_fixed_river_bucket_strategy(
      native.value(), native_solved.value().average_strategy, decision.source_public_node,
      query_bucket.members[0].combo);
  const auto second_query = gtosd::query_fixed_river_bucket_strategy(
      native.value(), native_solved.value().average_strategy, decision.source_public_node,
      query_bucket.members[1].combo);
  require(first_query.has_value() && second_query.has_value() &&
              first_query.value().actions == second_query.value().actions &&
              first_query.value().probabilities == second_query.value().probabilities,
          "bucket-native strategy expands identically to every physical combo in a bucket");

  const auto partial = gtosd::solve_fixed_river_bucket_game(native.value(), production_config(64U));
  const auto serialized_partial =
      partial ? gtosd::serialize_solver_checkpoint(partial.value().checkpoint)
              : gtosd::Result<std::string, gtosd::SolverError>::failure(
                    gtosd::SolverError::InvalidCheckpoint);
  const auto restored_partial =
      serialized_partial ? gtosd::deserialize_solver_checkpoint(serialized_partial.value())
                         : gtosd::Result<gtosd::SolverCheckpoint, gtosd::SolverError>::failure(
                               gtosd::SolverError::InvalidCheckpoint);
  const auto checkpoint_path =
      std::filesystem::temp_directory_path() / "gtosd-river-bucket-native-checkpoint.bin";
  auto checkpoint_temporary = checkpoint_path;
  checkpoint_temporary += ".tmp";
  std::error_code checkpoint_ignored;
  std::filesystem::remove(checkpoint_path, checkpoint_ignored);
  std::filesystem::remove(checkpoint_temporary, checkpoint_ignored);
  const auto saved_partial =
      partial
          ? gtosd::save_fixed_river_bucket_checkpoint(native.value(), partial.value().checkpoint,
                                                      checkpoint_path.string())
          : gtosd::Result<bool, gtosd::SolverError>::failure(gtosd::SolverError::InvalidCheckpoint);
  const auto loaded_partial =
      saved_partial
          ? gtosd::load_fixed_river_bucket_checkpoint(native.value(), checkpoint_path.string())
          : gtosd::Result<gtosd::SolverCheckpoint, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidCheckpoint);
  require(loaded_partial.has_value() && restored_partial.has_value() &&
              gtosd::serialize_solver_checkpoint(loaded_partial.value()).value() ==
                  gtosd::serialize_solver_checkpoint(restored_partial.value()).value() &&
              !std::filesystem::exists(checkpoint_temporary),
          "bucket-native checkpoint saves atomically and restores the exact payload");
  auto mismatched_game = native.value();
  mismatched_game.abstraction_fingerprint += "-mismatch";
  const auto mismatched_load =
      gtosd::load_fixed_river_bucket_checkpoint(mismatched_game, checkpoint_path.string());
  require(!mismatched_load && mismatched_load.error() == gtosd::SolverError::InvalidCheckpoint,
          "bucket-native checkpoint rejects a different abstraction identity");
  {
    std::fstream corrupted(checkpoint_path, std::ios::binary | std::ios::in | std::ios::out);
    corrupted.seekg(-2, std::ios::end);
    char byte = '\0';
    corrupted.read(&byte, 1);
    byte ^= 1;
    corrupted.seekp(-2, std::ios::end);
    corrupted.write(&byte, 1);
  }
  const auto corrupted_load =
      gtosd::load_fixed_river_bucket_checkpoint(native.value(), checkpoint_path.string());
  require(!corrupted_load && corrupted_load.error() == gtosd::SolverError::InvalidCheckpoint,
          "bucket-native checkpoint rejects payload corruption by checksum");
  std::filesystem::remove(checkpoint_path, checkpoint_ignored);
  const auto resumed =
      loaded_partial ? gtosd::solve_fixed_river_bucket_game(native.value(), production_config(128U),
                                                            &loaded_partial.value())
                     : gtosd::Result<gtosd::SolveResult, gtosd::SolverError>::failure(
                           gtosd::SolverError::InvalidCheckpoint);
  const auto continuous =
      gtosd::solve_fixed_river_bucket_game(native.value(), production_config(128U));
  const auto serialized_resumed =
      resumed ? gtosd::serialize_solver_checkpoint(resumed.value().checkpoint)
              : gtosd::Result<std::string, gtosd::SolverError>::failure(
                    gtosd::SolverError::InvalidCheckpoint);
  const auto serialized_continuous =
      continuous ? gtosd::serialize_solver_checkpoint(continuous.value().checkpoint)
                 : gtosd::Result<std::string, gtosd::SolverError>::failure(
                       gtosd::SolverError::InvalidCheckpoint);
  require(serialized_resumed.has_value() && serialized_continuous.has_value() &&
              serialized_resumed.value() == serialized_continuous.value(),
          "bucket-native checkpoint resume is byte-identical to continuous ProductionDcfr");
  require(partial.value().checkpoint.game_fingerprint == native.value().game_fingerprint &&
              partial.value().checkpoint.game_fingerprint !=
                  projection.value().source_game_fingerprint,
          "bucket-native and exact postflop checkpoints have distinct identities");
  auto raked_config = config;
  raked_config.rake = {true,
                       gtosd::RangeWeight::from_basis_points(500).value(),
                       gtosd::Money::from_antes(3).value(),
                       true,
                       {}};
  const auto raked_native =
      gtosd::build_fixed_river_bucket_game(raked_config, ranges, {64U, 64U, 100'000U});
  require(raked_native.has_value() &&
              raked_native.value().source_game_fingerprint !=
                  native.value().source_game_fingerprint &&
              raked_native.value().game_fingerprint != native.value().game_fingerprint,
          "bucket-native fingerprint includes the complete config and separates rake rules");

  auto invalid_solver = production_config(8U);
  invalid_solver.algorithm = gtosd::SolverAlgorithm::CfrPlus;
  require(!gtosd::solve_fixed_river_bucket_game(native.value(), invalid_solver),
          "bucket-native product wrapper rejects non-ProductionDcfr algorithms");
  auto zero_iterations = production_config(0U);
  require(!gtosd::solve_fixed_river_bucket_game(native.value(), zero_iterations),
          "bucket-native product wrapper rejects zero iterations");
  auto invalid_street = config;
  invalid_street.river.reset();
  require(!gtosd::build_fixed_river_bucket_game(invalid_street, ranges),
          "bucket-native builder rejects games before the fixed river");
  require(!gtosd::build_fixed_river_bucket_game(config, ranges, {1U, 64U, 100'000U}),
          "bucket-native builder fails closed when the physical preprocessing cap is exceeded");

  const double native_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - native_started).count();

  const auto root = std::ranges::find_if(projection.value().public_states, [&](const auto &state) {
    return state.source_public_node == projection.value().source_root;
  });
  const auto checked = std::ranges::find_if(
      root->actions, [](const auto &action) { return action.type == gtosd::ActionType::Check; });
  const auto subgame_node = root->child_public_nodes[static_cast<std::size_t>(
      std::distance(root->actions.begin(), checked))];
  const auto definition =
      gtosd::define_projected_postflop_subgame(projection.value(), subgame_node, 0U);
  require(definition.has_value() &&
              gtosd::validate_public_subgame(abstract_game.value(), definition.value()),
          "the same physical public boundary remains closed after bucketing");
  const auto boundary = gtosd::derive_subgame_boundary(abstract_game.value(), definition.value(),
                                                       abstract_blueprint.value(),
                                                       projection.value().source_iterations);
  const auto gadget = boundary ? gtosd::build_safe_resolving_gadget(
                                     abstract_game.value(), definition.value(), boundary.value())
                               : gtosd::Result<gtosd::FiniteGame, gtosd::SolverError>::failure(
                                     gtosd::SolverError::InvalidSubgame);
  const auto resolved = gadget
                            ? gtosd::solve_finite_game(gadget.value(), production_config(20'000U))
                            : gtosd::Result<gtosd::SolveResult, gtosd::SolverError>::failure(
                                  gtosd::SolverError::InvalidSubgame);
  const auto abstract_spliced =
      resolved
          ? gtosd::splice_resolved_subgame_strategy(abstract_game.value(), definition.value(),
                                                    boundary.value(), abstract_blueprint.value(),
                                                    resolved.value().average_strategy)
          : gtosd::Result<gtosd::StrategyProfile, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidStrategy);
  const auto lifted = abstract_spliced
                          ? gtosd::lift_strategy_profile(projection.value().game, policy.value(),
                                                         abstract_spliced.value())
                          : gtosd::Result<gtosd::StrategyProfile, gtosd::SolverError>::failure(
                                gtosd::SolverError::InvalidStrategy);
  require(lifted.has_value(), "bucketed subgame strategy lifts to every original infoset");
  const auto original_before =
      gtosd::calculate_nash_conv(projection.value().game, projection.value().blueprint);
  const auto bucket_only = gtosd::lift_strategy_profile(projection.value().game, policy.value(),
                                                        abstract_blueprint.value());
  const auto bucket_certification =
      bucket_only ? gtosd::calculate_nash_conv(projection.value().game, bucket_only.value())
                  : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                        gtosd::SolverError::InvalidStrategy);
  const auto combined = gtosd::calculate_nash_conv(projection.value().game, lifted.value());
  require(original_before.has_value() && bucket_certification.has_value() && combined.has_value() &&
              std::isfinite(combined.value().normalized_nash_conv),
          "exact, bucket-only and bucket-plus-subgame are certified in the original game");

  std::cout << "POSTFLOP_SUBGAME_COMPOSITION fixture=R2S-RIVER-SPARSE-001"
            << " exact_infosets=" << summary.value().original_information_sets
            << " bucket_infosets=" << summary.value().abstract_information_sets
            << " physical_deals=" << native.value().work_model.physical_deals_preprocessed
            << " bucket_pairs=" << native.value().work_model.bucket_pairs << " physical_pass_nodes="
            << native.value().work_model.physical_node_instances_per_player_pass
            << " bucket_pass_nodes="
            << native.value().work_model.bucket_node_instances_per_player_pass
            << " native_bytes=" << native.value().byte_model.total_bytes
            << " native_seconds=" << native_seconds
            << " exact_nash_conv=" << original_before.value().normalized_nash_conv
            << " bucket_nash_conv=" << bucket_certification.value().normalized_nash_conv
            << " native_original_nash_conv="
            << native_original_certification.value().normalized_nash_conv
            << " combined_nash_conv=" << combined.value().normalized_nash_conv << '\n';
}

void test_full_range_river_bucket_native_scaling() {
  const auto config = make_river_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  const auto build_started = std::chrono::steady_clock::now();
  const auto native = gtosd::build_fixed_river_bucket_game(config, ranges);
  const double build_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - build_started).count();
  require(native.has_value(), "full-range bucket-native fixed-river game builds");
  require(native.value().work_model.physical_deals_preprocessed > 100'000U &&
              native.value().work_model.bucket_pairs > 1U &&
              native.value().work_model.bucket_pairs <
                  native.value().work_model.physical_deals_preprocessed,
          "full-range river aggregates physical deals into multiple exact-value bucket pairs");
  require(native.value().work_model.bucket_node_instances_per_player_pass <
                  native.value().work_model.physical_node_instances_per_player_pass &&
              native.value().work_model.bucket_action_entries_per_player_pass <
                  native.value().work_model.physical_action_entries_per_player_pass,
          "full-range bucket-native hot path is smaller than physical deal traversal");

  const auto solve_started = std::chrono::steady_clock::now();
  const auto solved = gtosd::solve_fixed_river_bucket_game(native.value(), production_config(32U));
  const double solve_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - solve_started).count();
  require(solved.has_value() && solved.value().maximum_normalization_error <= 1.0e-12 &&
              !native.value().validation_game.has_value() &&
              native.value().byte_model.finite_game_bytes == 0U,
          "full-range bucket-native ProductionDcfr solve omits the validation-only product game");

  std::cout << "RIVER_BUCKET_NATIVE fixture=RIVER-FULL-RANGE-001"
            << " physical_deals=" << native.value().work_model.physical_deals_preprocessed
            << " bucket_pairs=" << native.value().work_model.bucket_pairs
            << " player0_buckets=" << native.value().player_buckets[0].size()
            << " player1_buckets=" << native.value().player_buckets[1].size()
            << " public_nodes=" << native.value().work_model.public_nodes_per_pair
            << " physical_pass_nodes="
            << native.value().work_model.physical_node_instances_per_player_pass
            << " bucket_pass_nodes="
            << native.value().work_model.bucket_node_instances_per_player_pass
            << " bytes=" << native.value().byte_model.total_bytes
            << " build_seconds=" << build_seconds << " solve32_seconds=" << solve_seconds << '\n';
}

void test_exact_blocker_signature_is_lossless_and_splits_blockers() {
  const auto config = make_river_config();
  const auto collision_ranges = make_made_hand_collision_ranges(config);
  gtosd::PostflopRiverBucketBuildOptions exact_blocker_options;
  exact_blocker_options.maximum_physical_deals = 64U;
  exact_blocker_options.maximum_bucket_pairs = 64U;
  exact_blocker_options.maximum_materialized_validation_nodes = 100'000U;
  exact_blocker_options.materialize_validation_game = true;
  exact_blocker_options.abstraction =
      gtosd::PostflopRiverBucketAbstraction::ExactBlockerSignatureV2;
  const auto exact_blocker =
      gtosd::build_fixed_river_bucket_game(config, collision_ranges, exact_blocker_options);
  require(exact_blocker.has_value() && exact_blocker.value().validation_game.has_value() &&
              exact_blocker.value().abstraction ==
                  gtosd::PostflopRiverBucketAbstraction::ExactBlockerSignatureV2 &&
              exact_blocker.value().player_buckets[0].size() == 1U &&
              exact_blocker.value().player_buckets[1].size() == 1U,
          "exact blocker signatures merge hands only when compatibility and showdown agree");

  gtosd::PostflopProductionSolveRequest request;
  request.iterations = 256U;
  const auto production_options = gtosd::resolve_postflop_production_options(request);
  const auto exact =
      production_options
          ? gtosd::solve_postflop_exact(config, collision_ranges, production_options.value())
          : gtosd::Result<gtosd::PostflopSolveResult, gtosd::PostflopSolverError>::failure(
                gtosd::PostflopSolverError::InvalidConfiguration);
  const auto projection =
      exact ? gtosd::project_fixed_river_postflop_game(config, collision_ranges,
                                                       exact.value().checkpoint, {64U, 100'000U})
            : gtosd::Result<gtosd::PostflopSubgameProjection, gtosd::SolverError>::failure(
                  gtosd::SolverError::InvalidGame);
  const auto solved = gtosd::solve_fixed_river_bucket_game(exact_blocker.value(),
                                                           production_config(request.iterations));
  const auto abstract_certification =
      solved ? gtosd::calculate_nash_conv(*exact_blocker.value().validation_game,
                                          solved.value().average_strategy)
             : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                   gtosd::SolverError::InvalidStrategy);
  const auto lifted =
      projection && solved
          ? gtosd::lift_fixed_river_bucket_strategy(projection.value(), exact_blocker.value(),
                                                    solved.value().average_strategy)
          : gtosd::Result<gtosd::StrategyProfile, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidStrategy);
  const auto physical_certification =
      lifted ? gtosd::calculate_nash_conv(projection.value().game, lifted.value())
             : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                   gtosd::SolverError::InvalidStrategy);
  require(abstract_certification.has_value() && physical_certification.has_value(),
          "exact blocker quotient solves and lifts to the physical game");
  require_near(abstract_certification.value().normalized_nash_conv,
               physical_certification.value().normalized_nash_conv, 1.0e-12,
               "exact blocker quotient preserves unrestricted physical NashConv");
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    require_near(abstract_certification.value().profile_value[player],
                 physical_certification.value().profile_value[player], 1.0e-12,
                 "exact blocker quotient preserves physical profile value");
    require_near(abstract_certification.value().best_response_value[player],
                 physical_certification.value().best_response_value[player], 1.0e-12,
                 "exact blocker quotient preserves physical best response");
  }

  const auto blocker_ranges = make_blocker_sensitive_collision_ranges(config);
  const auto made_hand =
      gtosd::build_fixed_river_bucket_game(config, blocker_ranges, {64U, 64U, 100'000U});
  exact_blocker_options.materialize_validation_game = false;
  const auto blocker_exact =
      gtosd::build_fixed_river_bucket_game(config, blocker_ranges, exact_blocker_options);
  require(made_hand.has_value() && blocker_exact.has_value() &&
              made_hand.value().player_buckets[0].size() == 1U &&
              blocker_exact.value().player_buckets[0].size() == 3U &&
              made_hand.value().abstraction_fingerprint !=
                  blocker_exact.value().abstraction_fingerprint,
          "v2 splits equal made hands when their opponent compatibility vectors differ");

  const auto combos = gtosd::all_combos();
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    const auto opponent = static_cast<std::uint8_t>(1U - player);
    for (const auto &bucket : blocker_exact.value().player_buckets[player]) {
      for (std::size_t first = 0U; first < bucket.members.size(); ++first) {
        const auto first_mask = combos[bucket.members[first].combo].first.mask() |
                                combos[bucket.members[first].combo].second.mask();
        for (std::size_t second = first + 1U; second < bucket.members.size(); ++second) {
          const auto second_mask = combos[bucket.members[second].combo].first.mask() |
                                   combos[bucket.members[second].combo].second.mask();
          for (gtosd::ComboId opponent_combo = 0U; opponent_combo < combos.size();
               ++opponent_combo) {
            if (blocker_ranges.players[opponent][opponent_combo].basis_points() == 0U) {
              continue;
            }
            const auto opponent_mask =
                combos[opponent_combo].first.mask() | combos[opponent_combo].second.mask();
            require((first_mask & opponent_mask) == 0U == ((second_mask & opponent_mask) == 0U),
                    "members of one v2 class have identical opponent compatibility");
          }
        }
      }
    }
  }
}

void test_exact_blocker_signature_full_range_lower_bound() {
  const auto config = make_river_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  gtosd::PostflopRiverBucketBuildOptions options;
  options.maximum_bucket_pairs = 200'000U;
  options.abstraction = gtosd::PostflopRiverBucketAbstraction::ExactBlockerSignatureV2;
  const auto exact_blocker = gtosd::build_fixed_river_bucket_game(config, ranges, options);
  require(exact_blocker.has_value(),
          "full-range exact blocker quotient builds with an explicit physical-size cap");
  require(exact_blocker.value().player_buckets[0].size() == 465U &&
              exact_blocker.value().player_buckets[1].size() == 465U &&
              exact_blocker.value().work_model.bucket_pairs ==
                  exact_blocker.value().work_model.physical_deals_preprocessed,
          "every full-range river combo has a unique exact blocker signature");
  require(exact_blocker.value().work_model.bucket_node_instances_per_player_pass ==
                  exact_blocker.value().work_model.physical_node_instances_per_player_pass &&
              exact_blocker.value().work_model.bucket_action_entries_per_player_pass ==
                  exact_blocker.value().work_model.physical_action_entries_per_player_pass,
          "full-range lossless signatures have no traversal compression ceiling");
  std::cout << "RIVER_EXACT_BLOCKER_LOWER_BOUND fixture=RIVER-FULL-RANGE-001"
            << " physical_deals=" << exact_blocker.value().work_model.physical_deals_preprocessed
            << " exact_pairs=" << exact_blocker.value().work_model.bucket_pairs
            << " player0_classes=" << exact_blocker.value().player_buckets[0].size()
            << " player1_classes=" << exact_blocker.value().player_buckets[1].size()
            << " bytes=" << exact_blocker.value().byte_model.total_bytes << '\n';
}

void test_showdown_distribution_v3_is_versioned_and_blocker_sensitive() {
  const auto config = make_river_config();
  const auto ranges = make_blocker_sensitive_collision_ranges(config);

  gtosd::PostflopRiverBucketBuildOptions made_hand_options;
  made_hand_options.maximum_physical_deals = 64U;
  made_hand_options.maximum_bucket_pairs = 64U;
  const auto made_hand = gtosd::build_fixed_river_bucket_game(config, ranges, made_hand_options);

  auto exact_blocker_options = made_hand_options;
  exact_blocker_options.abstraction =
      gtosd::PostflopRiverBucketAbstraction::ExactBlockerSignatureV2;
  const auto exact_blocker =
      gtosd::build_fixed_river_bucket_game(config, ranges, exact_blocker_options);

  auto distribution_options = made_hand_options;
  distribution_options.abstraction = gtosd::PostflopRiverBucketAbstraction::ShowdownDistributionV3;
  distribution_options.distribution_quantization_basis_points = 500U;
  const auto distribution =
      gtosd::build_fixed_river_bucket_game(config, ranges, distribution_options);
  const auto repeated = gtosd::build_fixed_river_bucket_game(config, ranges, distribution_options);

  require(made_hand.has_value() && exact_blocker.has_value() && distribution.has_value() &&
              repeated.has_value(),
          "all three River abstraction versions build on blocker-sensitive ranges");
  require(distribution.value().abstraction ==
                  gtosd::PostflopRiverBucketAbstraction::ShowdownDistributionV3 &&
              std::string(gtosd::postflop_river_bucket_abstraction_name(
                  distribution.value().abstraction)) == "showdown_distribution_v3" &&
              distribution.value().abstraction_fingerprint ==
                  repeated.value().abstraction_fingerprint,
          "v3 has a stable identity and deterministic partition fingerprint");
  require(distribution.value().player_buckets[0].size() >
                  made_hand.value().player_buckets[0].size() &&
              distribution.value().player_buckets[0].size() <=
                  exact_blocker.value().player_buckets[0].size(),
          "v3 distinguishes blocker distributions without exceeding exact blocker classes");
  require(distribution.value().abstraction_fingerprint !=
                  made_hand.value().abstraction_fingerprint &&
              distribution.value().abstraction_fingerprint !=
                  exact_blocker.value().abstraction_fingerprint,
          "v3 cannot share checkpoints with earlier abstractions");

  auto invalid_options = distribution_options;
  invalid_options.distribution_quantization_basis_points = 0U;
  require(!gtosd::build_fixed_river_bucket_game(config, ranges, invalid_options).has_value(),
          "v3 rejects a zero distribution quantum");
}

void test_river_equitable_partition_feasibility() {
  const auto config = make_river_config();
  const auto collision = gtosd::analyze_fixed_river_equitable_partition(
      config, make_made_hand_collision_ranges(config), 64U);
  require(collision.has_value() && collision.value().partition_is_equitable &&
              collision.value().stable_classes[0] == 1U &&
              collision.value().stable_classes[1] == 1U &&
              collision.value().compatible_class_pairs == 1U,
          "equitable refinement preserves a controlled lossless collision");

  const auto full =
      gtosd::analyze_fixed_river_equitable_partition(config, gtosd::make_uniform_postflop_ranges());
  require(full.has_value() && full.value().partition_is_equitable &&
              full.value().active_combos[0] == 465U && full.value().active_combos[1] == 465U &&
              full.value().physical_deals == 188'790U,
          "full-range equitable refinement accounts for every active combo and compatible deal");
  require(full.value().initial_classes[0] <= full.value().stable_classes[0] &&
              full.value().stable_classes[0] <= full.value().active_combos[0] &&
              full.value().initial_classes[1] <= full.value().stable_classes[1] &&
              full.value().stable_classes[1] <= full.value().active_combos[1] &&
              full.value().compatible_class_pairs <= full.value().physical_deals &&
              full.value().strategic_row_reduction >= 1.0 &&
              full.value().deal_pair_reduction >= 1.0,
          "equitable refinement reports conservative non-expanding quotient counts");
  std::cout << "RIVER_EQUITABLE_FEASIBILITY fixture=RIVER-FULL-RANGE-001"
            << " active0=" << full.value().active_combos[0]
            << " active1=" << full.value().active_combos[1]
            << " initial0=" << full.value().initial_classes[0]
            << " initial1=" << full.value().initial_classes[1]
            << " stable0=" << full.value().stable_classes[0]
            << " stable1=" << full.value().stable_classes[1]
            << " class_pairs=" << full.value().compatible_class_pairs
            << " deals=" << full.value().physical_deals
            << " row_reduction=" << full.value().strategic_row_reduction
            << " pair_reduction=" << full.value().deal_pair_reduction
            << " rounds=" << full.value().refinement_rounds << '\n';
}

void test_medium_river_bucket_native_original_game_oracle() {
  const std::array benchmarks{gtosd::PostflopBenchmark::PfF1, gtosd::PostflopBenchmark::PfF2};
  for (const auto benchmark : benchmarks) {
    const auto config = make_river_config(benchmark);
    const auto ranges = make_medium_weighted_ranges(config);
    gtosd::PostflopProductionSolveRequest exact_request;
    exact_request.iterations = 128U;
    const auto exact_options = gtosd::resolve_postflop_production_options(exact_request);
    const auto exact_started = std::chrono::steady_clock::now();
    const auto exact =
        exact_options
            ? gtosd::solve_postflop_exact(config, ranges, exact_options.value())
            : gtosd::Result<gtosd::PostflopSolveResult, gtosd::PostflopSolverError>::failure(
                  gtosd::PostflopSolverError::InvalidConfiguration);
    const double exact_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - exact_started).count();
    require(exact.has_value() && !exact.value().convergence.empty(),
            "medium weighted exact ProductionDcfr oracle solves");

    const auto projection = gtosd::project_fixed_river_postflop_game(
        config, ranges, exact.value().checkpoint, {20'000U, 500'000U});
    const auto native =
        gtosd::build_fixed_river_bucket_game(config, ranges, {20'000U, 4'096U, 500'000U, true});
    require(projection.has_value() && native.has_value(),
            "medium weighted physical oracle and bucket-native game build");
    require(projection.value().physical_deals ==
                    native.value().work_model.physical_deals_preprocessed &&
                native.value().work_model.bucket_pairs < projection.value().physical_deals,
            "medium weighted bucket pairs preserve and compress compatible deals");

    const auto policy = gtosd::make_projected_postflop_card_abstraction(projection.value());
    const auto physical_abstract =
        policy ? gtosd::apply_card_abstraction(projection.value().game, policy.value())
               : gtosd::Result<gtosd::FiniteGame, gtosd::SolverError>::failure(
                     gtosd::SolverError::InvalidAbstraction);
    const auto native_started = std::chrono::steady_clock::now();
    const auto native_solved =
        gtosd::solve_fixed_river_bucket_game(native.value(), production_config(128U));
    const double native_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - native_started).count();
    const auto physical_solved =
        physical_abstract
            ? gtosd::solve_finite_game(physical_abstract.value(), production_config(128U))
            : gtosd::Result<gtosd::SolveResult, gtosd::SolverError>::failure(
                  gtosd::SolverError::InvalidAbstraction);
    require(policy.has_value() && physical_abstract.has_value() && native_solved.has_value() &&
                physical_solved.has_value(),
            "medium weighted native and physical abstract games solve");
    for (const auto &[information_set, native_strategy] : native_solved.value().average_strategy) {
      const auto physical_strategy = physical_solved.value().average_strategy.find(information_set);
      require(physical_strategy != physical_solved.value().average_strategy.end() &&
                  physical_strategy->second.probabilities.size() ==
                      native_strategy.probabilities.size(),
              "medium weighted strategy domains agree");
      for (std::size_t action = 0U; action < native_strategy.probabilities.size(); ++action) {
        require_near(native_strategy.probabilities[action],
                     physical_strategy->second.probabilities[action], 1.0e-10,
                     "medium weighted native strategy matches physical abstract traversal");
      }
    }

    const auto lifted = gtosd::lift_fixed_river_bucket_strategy(
        projection.value(), native.value(), native_solved.value().average_strategy);
    const auto original_certification =
        lifted ? gtosd::calculate_nash_conv(projection.value().game, lifted.value())
               : gtosd::Result<gtosd::NashConvResult, gtosd::SolverError>::failure(
                     gtosd::SolverError::InvalidStrategy);
    require(original_certification.has_value() &&
                std::isfinite(original_certification.value().normalized_nash_conv),
            "medium weighted bucket-native strategy has an exact original-game certificate");

    std::cout << "RIVER_BUCKET_ORACLE fixture=RIVER-MEDIUM-WEIGHTED-00"
              << (benchmark == gtosd::PostflopBenchmark::PfF1 ? 1 : 2)
              << " physical_deals=" << projection.value().physical_deals
              << " bucket_pairs=" << native.value().bucket_pairs.size()
              << " physical_nodes=" << projection.value().game.nodes.size()
              << " bucket_nodes=" << native.value().validation_game->nodes.size()
              << " physical_bytes=" << projection.value().byte_model.total_bytes
              << " bucket_bytes=" << native.value().byte_model.total_bytes
              << " exact_seconds=" << exact_seconds << " native_seconds=" << native_seconds
              << " exact_original_nash_conv="
              << exact.value().convergence.back().normalized_nash_conv
              << " bucket_original_nash_conv="
              << original_certification.value().normalized_nash_conv << '\n';
  }
}

} // namespace

int main() {
  try {
    test_postflop_projection_and_safe_resolving();
    test_bucket_and_subgame_composition_is_measured_in_original_game();
    test_full_range_river_bucket_native_scaling();
    test_exact_blocker_signature_is_lossless_and_splits_blockers();
    test_exact_blocker_signature_full_range_lower_bound();
    test_showdown_distribution_v3_is_versioned_and_blocker_sensitive();
    test_river_equitable_partition_feasibility();
    test_medium_river_bucket_native_original_game_oracle();
    std::cout << "POSTFLOP_SUBGAME_TESTS=PASS\nassertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "POSTFLOP_SUBGAME_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
