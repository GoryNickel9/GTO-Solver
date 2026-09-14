#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"
#include "gtosd/solver/subgame.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool ends_with(const std::string &value, const std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

gtosd::PublicSubgameDefinition checked_kuhn_public_state(const gtosd::FiniteGame &game) {
  gtosd::PublicSubgameDefinition definition;
  definition.public_state_id = "kuhn.after_p0_check.v1";
  definition.entry_history = "check";
  definition.resolving_player = 0;
  for (gtosd::GameNodeId node_id = 0U; node_id < game.nodes.size(); ++node_id) {
    const auto &node = game.nodes[node_id];
    if (node.kind == gtosd::GameNodeKind::Decision && node.player == 1U &&
        ends_with(node.information_set, ":k")) {
      definition.roots.push_back(node_id);
    }
  }
  return definition;
}

gtosd::SubgameBoundary valid_boundary(const gtosd::FiniteGame &game) {
  gtosd::SubgameBoundary boundary;
  boundary.game_fingerprint = gtosd::finite_game_fingerprint(game);
  boundary.blueprint_fingerprint = "fnv1a64:blueprint-test";
  boundary.public_state_id = "kuhn.after_p0_check.v1";
  boundary.algorithm = "ProductionDcfr";
  boundary.blueprint_iterations = 8'000;
  boundary.opponent = 1;
  boundary.values = {
      {"kuhn:p1:J:k", 0.2, -0.25, gtosd::BoundaryReachStatus::Positive},
      {"kuhn:p1:K:k", 0.8, 0.5, gtosd::BoundaryReachStatus::Positive},
      {"kuhn:p1:Q:k", 0.0, 0.0, gtosd::BoundaryReachStatus::Zero},
  };
  const auto definition = checked_kuhn_public_state(game);
  for (const auto root : definition.roots) {
    const auto &information_set = game.nodes[root].information_set;
    const double reach = information_set == "kuhn:p1:J:k"   ? 0.1
                         : information_set == "kuhn:p1:K:k" ? 0.4
                                                            : 0.0;
    boundary.root_reaches.push_back({root, information_set, reach});
  }
  return boundary;
}

void test_public_state_closure_and_boundary_round_trip() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto definition = checked_kuhn_public_state(game);
  const auto summary = gtosd::validate_public_subgame(game, definition);
  require(summary.has_value() && summary.value().information_set_closed,
          "public root is closed over every information set occurrence");
  require(summary.value().root_nodes == 6U && summary.value().boundary_information_sets == 3U,
          "all private deals are present while boundary keys use private information sets");

  const auto boundary = valid_boundary(game);
  require(gtosd::validate_subgame_boundary(game, definition, boundary).has_value(),
          "asymmetric and zero-reach boundary validates");

  const auto definition_bytes = gtosd::serialize_public_subgame_definition(definition);
  const auto restored_definition =
      definition_bytes ? gtosd::deserialize_public_subgame_definition(definition_bytes.value())
                       : gtosd::Result<gtosd::PublicSubgameDefinition, gtosd::SolverError>::failure(
                             gtosd::SolverError::InvalidSubgame);
  require(restored_definition.has_value() && restored_definition.value() == definition,
          "public-state definition round-trips");

  const auto boundary_bytes = gtosd::serialize_subgame_boundary(boundary);
  const auto restored_boundary =
      boundary_bytes ? gtosd::deserialize_subgame_boundary(boundary_bytes.value())
                     : gtosd::Result<gtosd::SubgameBoundary, gtosd::SolverError>::failure(
                           gtosd::SolverError::InvalidSubgame);
  require(restored_boundary.has_value() && restored_boundary.value() == boundary,
          "boundary values round-trip without losing reach or CFV precision");

  const auto byte_model = gtosd::estimate_subgame_boundary_bytes(game, definition, boundary);
  require(byte_model.has_value() && byte_model.value().dense_record_bytes == 168U &&
              byte_model.value().minimum_runtime_bytes == 168U,
          "byte model charges private boundary and physical root-reach records");
}

void test_incomplete_or_incompatible_boundaries_are_rejected() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto definition = checked_kuhn_public_state(game);

  auto incomplete = valid_boundary(game);
  incomplete.values.pop_back();
  require(!gtosd::validate_subgame_boundary(game, definition, incomplete),
          "incomplete private boundary is rejected");

  auto wrong_game = valid_boundary(game);
  wrong_game.game_fingerprint = "fnv1a64:wrong-game";
  require(!gtosd::validate_subgame_boundary(game, definition, wrong_game),
          "boundary from another game is rejected");

  auto wrong_algorithm = valid_boundary(game);
  wrong_algorithm.algorithm = "CfrPlus";
  require(!gtosd::validate_subgame_boundary(game, definition, wrong_algorithm),
          "non-ProductionDcfr blueprint is rejected");

  auto invented_zero_reach = valid_boundary(game);
  invented_zero_reach.values.back().reach_status = gtosd::BoundaryReachStatus::Positive;
  require(!gtosd::validate_subgame_boundary(game, definition, invented_zero_reach),
          "zero reach must remain explicit");

  auto all_zero = valid_boundary(game);
  for (auto &value : all_zero.values) {
    value.counterfactual_reach = 0.0;
    value.reach_status = gtosd::BoundaryReachStatus::Zero;
  }
  require(!gtosd::validate_subgame_boundary(game, definition, all_zero),
          "public state with no positive reach is rejected");
}

void test_exact_boundary_derivation_uses_counterfactual_reach() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto definition = checked_kuhn_public_state(game);
  const auto uniform = gtosd::uniform_strategy_profile(game);
  require(uniform.has_value(), "uniform blueprint is available");
  auto blueprint = uniform.value();

  for (auto &[information_set, strategy] : blueprint) {
    if (information_set == "kuhn:p0:J:") {
      strategy.probabilities = {1.0, 0.0};
    } else if (information_set == "kuhn:p0:Q:" || information_set == "kuhn:p0:K:") {
      strategy.probabilities = {0.0, 1.0};
    }
  }

  const auto boundary = gtosd::derive_subgame_boundary(game, definition, blueprint, 400U);
  require(boundary.has_value(), "boundary derives from a complete blueprint");
  require(boundary.value().algorithm == "ProductionDcfr" &&
              boundary.value().blueprint_iterations == 400U,
          "derived boundary records the mandated algorithm and iteration");
  const auto fingerprint = gtosd::strategy_profile_fingerprint(game, blueprint);
  require(fingerprint.has_value() && boundary.value().blueprint_fingerprint == fingerprint.value(),
          "derived boundary is bound to the full strategy profile");

  bool saw_zero = false;
  bool saw_positive = false;
  for (const auto &value : boundary.value().values) {
    require(std::isfinite(value.blueprint_counterfactual_value),
            "derived conditional counterfactual value is finite");
    if (value.opponent_information_set == "kuhn:p1:J:k") {
      saw_zero = value.reach_status == gtosd::BoundaryReachStatus::Zero &&
                 value.counterfactual_reach == 0.0 && value.blueprint_counterfactual_value == 0.0;
    } else {
      saw_positive = saw_positive || (value.reach_status == gtosd::BoundaryReachStatus::Positive &&
                                      value.counterfactual_reach > 0.0);
    }
  }
  require(saw_zero && saw_positive,
          "derivation preserves a private zero-reach state beside positive states");
}

void test_safe_gadget_and_resolving_player_only_splice() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto definition = checked_kuhn_public_state(game);
  const auto blueprint = gtosd::uniform_strategy_profile(game);
  require(blueprint.has_value(), "uniform safe-resolving blueprint is available");
  const auto boundary = gtosd::derive_subgame_boundary(game, definition, blueprint.value(), 1U);
  require(boundary.has_value(), "safe gadget boundary derives");
  const auto gadget = boundary
                          ? gtosd::build_safe_resolving_gadget(game, definition, boundary.value())
                          : gtosd::Result<gtosd::FiniteGame, gtosd::SolverError>::failure(
                                gtosd::SolverError::InvalidSubgame);
  require(gadget.has_value(), "opt-out gadget is a valid finite game");

  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::ProductionDcfr;
  config.iterations = 50'000U;
  config.dcfr = {1.5, 0.0, 3.0};
  const auto resolved = gtosd::solve_finite_game(gadget.value(), config);
  require(resolved.has_value(), "ProductionDcfr solves the opt-out gadget");
  const auto spliced =
      resolved ? gtosd::splice_resolved_subgame_strategy(game, definition, boundary.value(),
                                                         blueprint.value(),
                                                         resolved.value().average_strategy)
               : gtosd::Result<gtosd::StrategyProfile, gtosd::SolverError>::failure(
                     gtosd::SolverError::InvalidStrategy);
  require(spliced.has_value(), "resolved player strategy splices into the original game");

  for (const auto &[information_set, strategy] : blueprint.value()) {
    if (strategy.player == boundary.value().opponent) {
      require(spliced.value().at(information_set) == strategy,
              "splice never replaces the opponent blueprint strategy");
    }
  }
  const auto before =
      gtosd::exact_best_response(game, blueprint.value(), boundary.value().opponent);
  const auto after = gtosd::exact_best_response(game, spliced.value(), boundary.value().opponent);
  require(before.has_value() && after.has_value(), "global opponent BR evaluates before/after");
  require(after.value().value <= before.value().value + 1.0e-3,
          "opt-out resolving does not increase global opponent BR beyond solve tolerance");
}

void test_atomic_checked_persistence() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto definition = checked_kuhn_public_state(game);
  const auto boundary = valid_boundary(game);
  const auto directory = std::filesystem::temp_directory_path();
  const auto definition_path = directory / "gtosd-public-subgame-contract-test.bin";
  const auto boundary_path = directory / "gtosd-subgame-boundary-contract-test.bin";

  require(gtosd::save_public_subgame_definition(definition, definition_path.string()).has_value(),
          "public state saves through atomic sibling replacement");
  require(gtosd::save_subgame_boundary(boundary, boundary_path.string()).has_value(),
          "boundary saves through atomic sibling replacement");
  const auto loaded_definition = gtosd::load_public_subgame_definition(definition_path.string());
  const auto loaded_boundary = gtosd::load_subgame_boundary(boundary_path.string());
  require(loaded_definition.has_value() && loaded_definition.value() == definition,
          "checked public-state file round-trips");
  require(loaded_boundary.has_value() && loaded_boundary.value() == boundary,
          "checked boundary file round-trips");

  {
    std::ofstream corrupt(boundary_path, std::ios::binary | std::ios::trunc);
    corrupt << "GTOSD_SUBGAME_BOUNDARY_FILE 4 deadbeefdeadbeef\nBAD!";
  }
  require(!gtosd::load_subgame_boundary(boundary_path.string()), "checksum mismatch is rejected");
  std::error_code ignored;
  std::filesystem::remove(definition_path, ignored);
  std::filesystem::remove(boundary_path, ignored);
}

void test_short_deck_toy_global_br_gate() {
  const auto game_result = gtosd::make_short_deck_river_toy_game(0.0);
  require(game_result.has_value(), "zero-rake Short Deck toy is available");
  const auto &game = game_result.value();
  gtosd::PublicSubgameDefinition definition;
  definition.public_state_id = "short_deck_toy.after_p0_check.v1";
  definition.entry_history = "check";
  definition.resolving_player = 0;
  for (gtosd::GameNodeId node_id = 0U; node_id < game.nodes.size(); ++node_id) {
    const auto &node = game.nodes[node_id];
    if (node.kind == gtosd::GameNodeKind::Decision && node.player == 1U &&
        ends_with(node.information_set, ":k")) {
      definition.roots.push_back(node_id);
    }
  }
  require(gtosd::validate_public_subgame(game, definition).has_value(),
          "Short Deck public state is information-set closed");
  const auto blueprint = gtosd::uniform_strategy_profile(game);
  const auto boundary =
      blueprint ? gtosd::derive_subgame_boundary(game, definition, blueprint.value(), 1U)
                : gtosd::Result<gtosd::SubgameBoundary, gtosd::SolverError>::failure(
                      gtosd::SolverError::InvalidStrategy);
  require(boundary.has_value(), "Short Deck boundary derives");
  const auto gadget = gtosd::build_safe_resolving_gadget(game, definition, boundary.value());
  require(gadget.has_value(), "Short Deck opt-out gadget builds");

  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::ProductionDcfr;
  config.iterations = 50'000U;
  config.dcfr = {1.5, 0.0, 3.0};
  const auto resolved = gtosd::solve_finite_game(gadget.value(), config);
  const auto spliced =
      resolved ? gtosd::splice_resolved_subgame_strategy(game, definition, boundary.value(),
                                                         blueprint.value(),
                                                         resolved.value().average_strategy)
               : gtosd::Result<gtosd::StrategyProfile, gtosd::SolverError>::failure(
                     gtosd::SolverError::InvalidStrategy);
  require(spliced.has_value(), "Short Deck resolving strategy splices");
  const auto before = gtosd::exact_best_response(game, blueprint.value(), 1U);
  const auto after = gtosd::exact_best_response(game, spliced.value(), 1U);
  require(before.has_value() && after.has_value() &&
              after.value().value <= before.value().value + 1.0e-3,
          "Short Deck global opponent BR does not increase beyond tolerance");
}

void test_off_tree_and_split_information_sets_are_rejected() {
  const auto game = gtosd::make_kuhn_poker_game();
  auto off_tree = checked_kuhn_public_state(game);
  off_tree.roots.push_back(static_cast<gtosd::GameNodeId>(game.nodes.size()));
  require(!gtosd::validate_public_subgame(game, off_tree), "off-tree public root is rejected");

  auto split = checked_kuhn_public_state(game);
  split.roots.pop_back();
  require(!gtosd::validate_public_subgame(game, split),
          "public root cannot split occurrences of a private information set");

  const auto future = gtosd::deserialize_subgame_boundary("GTOSD_SUBGAME_BOUNDARY 99 0\n");
  require(!future && future.error() == gtosd::SolverError::UnsupportedSubgameVersion,
          "future boundary version is rejected explicitly");
  const auto incomplete_legacy =
      gtosd::deserialize_subgame_boundary("GTOSD_SUBGAME_BOUNDARY 1 0\n");
  require(!incomplete_legacy &&
              incomplete_legacy.error() == gtosd::SolverError::UnsupportedSubgameVersion,
          "unimplemented boundary minor version is rejected explicitly");
}

} // namespace

int main() {
  try {
    test_public_state_closure_and_boundary_round_trip();
    test_incomplete_or_incompatible_boundaries_are_rejected();
    test_exact_boundary_derivation_uses_counterfactual_reach();
    test_safe_gadget_and_resolving_player_only_splice();
    test_atomic_checked_persistence();
    test_short_deck_toy_global_br_gate();
    test_off_tree_and_split_information_sets_are_rejected();
    std::cout << "SUBGAME_CONTRACT_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "safety_status=global_opponent_br_checked\n"
              << "gadget=opt_out_zero_sum_reduced\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SUBGAME_CONTRACT_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "SUBGAME_CONTRACT_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
