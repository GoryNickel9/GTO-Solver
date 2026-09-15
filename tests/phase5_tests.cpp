#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"

#include <cmath>
#include <cstdint>
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

void require_near(const double actual, const double expected, const double tolerance,
                  const std::string_view message) {
  require(std::abs(actual - expected) <= tolerance, message);
}

void set_binary_strategy(gtosd::StrategyProfile &profile, const std::string &information_set,
                         const double first, const double second) {
  auto &strategy = profile.at(information_set);
  require(strategy.probabilities.size() == 2U, "binary information set expected");
  strategy.probabilities = {first, second};
}

gtosd::SolverConfig config(const gtosd::SolverAlgorithm algorithm, const std::uint64_t iterations) {
  gtosd::SolverConfig result;
  result.algorithm = algorithm;
  result.iterations = iterations;
  result.seed = 0x534f4c5645525f35ULL;
  if (algorithm == gtosd::SolverAlgorithm::ProductionDcfr) {
    result.dcfr = {1.5, 0.0, 3.0};
  }
  return result;
}

void test_reference_game_contracts() {
  const auto matching = gtosd::make_matching_pennies_game();
  const auto matching_summary = gtosd::validate_finite_game(matching);
  require(matching_summary.has_value(), "matching pennies validates");
  require(matching_summary.value().information_sets == 2U,
          "matching pennies has two information sets");
  require(matching_summary.value().chance_nodes == 0U, "matching pennies has no chance node");

  const auto kuhn = gtosd::make_kuhn_poker_game();
  const auto kuhn_summary = gtosd::validate_finite_game(kuhn);
  require(kuhn_summary.has_value(), "Kuhn validates");
  require(kuhn_summary.value().information_sets == 12U, "Kuhn has twelve information sets");
  require(kuhn_summary.value().chance_nodes == 1U, "Kuhn has exact root chance");

  const auto leduc = gtosd::make_leduc_poker_game();
  const auto leduc_summary = gtosd::validate_finite_game(leduc);
  require(leduc_summary.has_value(), "Leduc validates");
  require(leduc_summary.value().nodes > kuhn_summary.value().nodes,
          "Leduc exercises a larger multi-street tree");
  require(leduc_summary.value().chance_nodes > 1U, "Leduc includes public chance cards");

  auto invalid = matching;
  invalid.nodes[invalid.root].edges.front().child =
      static_cast<gtosd::GameNodeId>(invalid.nodes.size());
  const auto invalid_result = gtosd::validate_finite_game(invalid);
  require(!invalid_result && invalid_result.error() == gtosd::SolverError::InvalidNode,
          "invalid child rejected explicitly");
}

void test_reference_equilibria_and_infoset_aware_best_response() {
  const auto matching = gtosd::make_matching_pennies_game();
  const auto matching_equilibrium = gtosd::reference_equilibrium_strategy(matching);
  require(matching_equilibrium.has_value(), "matching equilibrium available");
  const auto matching_metrics = gtosd::calculate_nash_conv(matching, matching_equilibrium.value());
  require(matching_metrics.has_value(), "matching NashConv evaluates");
  require_near(matching_metrics.value().profile_value[0], 0.0, 1.0e-12,
               "matching equilibrium EV exact");
  require_near(matching_metrics.value().nash_conv, 0.0, 1.0e-12,
               "matching equilibrium NashConv exact");

  auto pure_first = matching_equilibrium.value();
  set_binary_strategy(pure_first, "matching:p0", 1.0, 0.0);
  const auto response = gtosd::exact_best_response(matching, pure_first, 1);
  require(response.has_value(), "matching exact best response succeeds");
  require(response.value().policy.size() == 1U,
          "same hidden-action infoset receives one best-response action");
  require_near(response.value().value, 1.0, 1.0e-12, "best response exploits pure heads");

  const auto kuhn = gtosd::make_kuhn_poker_game();
  const auto kuhn_equilibrium = gtosd::reference_equilibrium_strategy(kuhn);
  require(kuhn_equilibrium.has_value(), "Kuhn reference equilibrium available");
  const auto kuhn_metrics = gtosd::calculate_nash_conv(kuhn, kuhn_equilibrium.value());
  require(kuhn_metrics.has_value(), "Kuhn reference NashConv evaluates");
  require_near(kuhn_metrics.value().profile_value[0], -1.0 / 18.0, 1.0e-12,
               "Kuhn reference EV matches analytic value");
  require_near(kuhn_metrics.value().profile_value[1], 1.0 / 18.0, 1.0e-12,
               "Kuhn reference opponent EV matches analytic value");
  require_near(kuhn_metrics.value().nash_conv, 0.0, 1.0e-12, "Kuhn reference strategy is Nash");

  const auto uniform = gtosd::uniform_strategy_profile(kuhn);
  const auto uniform_metrics = gtosd::calculate_nash_conv(kuhn, uniform.value());
  require(uniform_metrics.has_value() && uniform_metrics.value().nash_conv > 0.1,
          "uniform Kuhn strategy has positive known exploitability");

  const auto root_outcomes =
      gtosd::evaluate_strategy_profile_by_root_chance(kuhn, kuhn_equilibrium.value());
  require(root_outcomes.has_value() && root_outcomes.value().outcome_values.size() == 6U &&
              root_outcomes.value().probabilities.size() == 6U,
          "root-chance evaluator preserves all six ordered Kuhn deals");
  require_near(root_outcomes.value().profile_value[0], kuhn_metrics.value().profile_value[0],
               1.0e-12, "root-chance outcome aggregation reproduces exact Kuhn EV");
  require_near(root_outcomes.value().profile_value[1], kuhn_metrics.value().profile_value[1],
               1.0e-12, "root-chance outcome aggregation reproduces opponent Kuhn EV");

  const auto no_root_chance =
      gtosd::evaluate_strategy_profile_by_root_chance(matching, matching_equilibrium.value());
  require(!no_root_chance && no_root_chance.error() == gtosd::SolverError::InvalidConfiguration,
          "per-outcome evaluator rejects games without root chance explicitly");

  auto incomplete = kuhn_equilibrium.value();
  incomplete.erase(incomplete.begin());
  const auto invalid_profile = gtosd::evaluate_strategy_profile_by_root_chance(kuhn, incomplete);
  require(!invalid_profile && invalid_profile.error() == gtosd::SolverError::InvalidStrategy,
          "per-outcome evaluator rejects incomplete profiles");
}

void test_exact_solver_variants() {
  const auto kuhn = gtosd::make_kuhn_poker_game();
  constexpr gtosd::SolverAlgorithm exact_algorithms[] = {
      gtosd::SolverAlgorithm::VanillaCfr, gtosd::SolverAlgorithm::CfrPlus,
      gtosd::SolverAlgorithm::LinearCfr, gtosd::SolverAlgorithm::Dcfr};
  for (const auto algorithm : exact_algorithms) {
    auto solver_config = config(algorithm, 20'000);
    if (algorithm == gtosd::SolverAlgorithm::CfrPlus) {
      solver_config.averaging_delay = 100;
    }
    const auto solved = gtosd::solve_finite_game(kuhn, solver_config);
    require(solved.has_value(), "exact CFR variant solves Kuhn");
    require(solved.value().maximum_normalization_error <= 1.0e-12,
            "average strategy remains normalized");
    const auto metrics = gtosd::calculate_nash_conv(kuhn, solved.value().average_strategy);
    require(metrics.has_value(), "exact CFR variant receives exact certification");
    require_near(metrics.value().profile_value[0], -1.0 / 18.0, 1.0e-3,
                 "exact CFR variant converges to Kuhn EV");
    require(metrics.value().nash_conv < 0.02, "exact CFR variant lowers Kuhn NashConv");
  }

  const auto sampled = gtosd::solve_finite_game(
      kuhn, config(gtosd::SolverAlgorithm::ExternalSamplingMccfr, 100'000));
  require(sampled.has_value(), "external-sampling MCCFR laboratory path runs");
  const auto sampled_metrics = gtosd::calculate_nash_conv(kuhn, sampled.value().average_strategy);
  require(sampled_metrics.has_value() && sampled_metrics.value().nash_conv < 0.12,
          "external-sampling MCCFR trends toward Kuhn equilibrium");
}

void test_checkpoint_resume_and_validation() {
  const auto kuhn = gtosd::make_kuhn_poker_game();
  const auto continuous =
      gtosd::solve_finite_game(kuhn, config(gtosd::SolverAlgorithm::Dcfr, 8'000));
  const auto first_half =
      gtosd::solve_finite_game(kuhn, config(gtosd::SolverAlgorithm::Dcfr, 4'000));
  require(continuous.has_value() && first_half.has_value(), "continuous and partial run succeed");

  const auto serialized = gtosd::serialize_solver_checkpoint(first_half.value().checkpoint);
  require(serialized.has_value(), "checkpoint serializes");
  const auto restored = gtosd::deserialize_solver_checkpoint(serialized.value());
  require(restored.has_value(), "checkpoint deserializes");
  const auto resumed = gtosd::solve_finite_game(kuhn, config(gtosd::SolverAlgorithm::Dcfr, 8'000),
                                                &restored.value());
  require(resumed.has_value(), "checkpoint resumes");
  const auto continuous_bytes = gtosd::serialize_solver_checkpoint(continuous.value().checkpoint);
  const auto resumed_bytes = gtosd::serialize_solver_checkpoint(resumed.value().checkpoint);
  require(continuous_bytes.has_value() && resumed_bytes.has_value() &&
              continuous_bytes.value() == resumed_bytes.value(),
          "resume is byte-equivalent to continuous deterministic run");

  const auto certified =
      gtosd::solve_with_certification(kuhn, config(gtosd::SolverAlgorithm::Dcfr, 8'000), 2'000);
  require(certified.has_value() && certified.value().convergence.size() == 4U,
          "certified solve records the requested convergence curve");
  require(certified.value().convergence.back().nash_conv <
              certified.value().convergence.front().nash_conv,
          "DCFR convergence curve decreases over the measured run");
  const auto certified_bytes =
      gtosd::serialize_solver_checkpoint(certified.value().solve.checkpoint);
  require(certified_bytes.has_value() && certified_bytes.value() == continuous_bytes.value(),
          "certification intervals do not alter deterministic solver state");

  const auto corrupt = gtosd::deserialize_solver_checkpoint("GTOSD_CFR_CHECKPOINT 99 0\n");
  require(!corrupt && corrupt.error() == gtosd::SolverError::UnsupportedCheckpointVersion,
          "future checkpoint major rejected");
  const auto future_minor = gtosd::deserialize_solver_checkpoint("GTOSD_CFR_CHECKPOINT 1 99\n");
  require(!future_minor && future_minor.error() == gtosd::SolverError::UnsupportedCheckpointVersion,
          "future checkpoint minor rejected when required features are unknown");

  const auto matching = gtosd::make_matching_pennies_game();
  const auto mismatch = gtosd::solve_finite_game(
      matching, config(gtosd::SolverAlgorithm::Dcfr, 8'000), &restored.value());
  require(!mismatch && mismatch.error() == gtosd::SolverError::GameMismatch,
          "checkpoint cannot resume against a different game");

  auto unsupported_threads = config(gtosd::SolverAlgorithm::Dcfr, 100);
  unsupported_threads.thread_count = 3;
  const auto rejected = gtosd::solve_finite_game(kuhn, unsupported_threads);
  require(!rejected && rejected.error() == gtosd::SolverError::InvalidConfiguration,
          "unsupported worker count is rejected instead of rounded silently");

  for (const std::uint32_t threads : {2U, 4U, 8U}) {
    auto parallel_config = config(gtosd::SolverAlgorithm::Dcfr, 250);
    parallel_config.thread_count = threads;
    const auto parallel = gtosd::solve_finite_game(kuhn, parallel_config);
    require(parallel.has_value(), "exact traversal supports requested worker count");
    const auto parallel_metrics =
        gtosd::calculate_nash_conv(kuhn, parallel.value().average_strategy);
    require(parallel_metrics.has_value() && std::isfinite(parallel_metrics.value().nash_conv),
            "parallel exact result remains certifiable");
  }

  auto parallel_continuous_config = config(gtosd::SolverAlgorithm::CfrPlus, 1'000);
  parallel_continuous_config.thread_count = 2;
  const auto parallel_continuous = gtosd::solve_finite_game(kuhn, parallel_continuous_config);
  auto parallel_half_config = parallel_continuous_config;
  parallel_half_config.iterations = 500;
  const auto parallel_half = gtosd::solve_finite_game(kuhn, parallel_half_config);
  const auto parallel_resumed =
      gtosd::solve_finite_game(kuhn, parallel_continuous_config,
                               parallel_half ? &parallel_half.value().checkpoint : nullptr);
  require(parallel_continuous.has_value() && parallel_half.has_value() &&
              parallel_resumed.has_value(),
          "parallel continuous and resumed runs succeed");
  const auto parallel_continuous_bytes =
      gtosd::serialize_solver_checkpoint(parallel_continuous.value().checkpoint);
  const auto parallel_resumed_bytes =
      gtosd::serialize_solver_checkpoint(parallel_resumed.value().checkpoint);
  require(parallel_continuous_bytes.has_value() && parallel_resumed_bytes.has_value() &&
              parallel_continuous_bytes.value() == parallel_resumed_bytes.value(),
          "parallel resume is byte-equivalent at the same worker count");

  const auto production_continuous =
      gtosd::solve_finite_game(kuhn, config(gtosd::SolverAlgorithm::ProductionDcfr, 32));
  const auto production_epoch =
      gtosd::solve_finite_game(kuhn, config(gtosd::SolverAlgorithm::ProductionDcfr, 16));
  const auto production_serialized =
      production_epoch ? gtosd::serialize_solver_checkpoint(production_epoch.value().checkpoint)
                       : gtosd::Result<std::string, gtosd::SolverError>::failure(
                             gtosd::SolverError::InvalidCheckpoint);
  const auto production_restored =
      production_serialized ? gtosd::deserialize_solver_checkpoint(production_serialized.value())
                            : gtosd::Result<gtosd::SolverCheckpoint, gtosd::SolverError>::failure(
                                  gtosd::SolverError::InvalidCheckpoint);
  const auto production_resumed =
      production_restored
          ? gtosd::solve_finite_game(kuhn, config(gtosd::SolverAlgorithm::ProductionDcfr, 32),
                                     &production_restored.value())
          : gtosd::Result<gtosd::SolveResult, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidCheckpoint);
  const auto production_continuous_bytes =
      production_continuous
          ? gtosd::serialize_solver_checkpoint(production_continuous.value().checkpoint)
          : gtosd::Result<std::string, gtosd::SolverError>::failure(
                gtosd::SolverError::InvalidCheckpoint);
  const auto production_resumed_bytes =
      production_resumed ? gtosd::serialize_solver_checkpoint(production_resumed.value().checkpoint)
                         : gtosd::Result<std::string, gtosd::SolverError>::failure(
                               gtosd::SolverError::InvalidCheckpoint);
  require(production_restored.has_value() && production_restored.value().config.algorithm ==
                                                 gtosd::SolverAlgorithm::ProductionDcfr,
          "ProductionDcfr checkpoint preserves its distinct algorithm identity");
  require(production_restored.has_value() &&
              production_restored.value().minor == gtosd::SolverCheckpoint::format_minor,
          "ProductionDcfr checkpoint uses the current feature minor");
  require(production_continuous_bytes.has_value() && production_resumed_bytes.has_value() &&
              production_continuous_bytes.value() == production_resumed_bytes.value(),
          "ProductionDcfr resume across an epoch boundary is byte-equivalent");

  auto invalid_production_threads = config(gtosd::SolverAlgorithm::ProductionDcfr, 32);
  invalid_production_threads.thread_count = 2U;
  auto invalid_production_gamma = config(gtosd::SolverAlgorithm::ProductionDcfr, 32);
  invalid_production_gamma.dcfr.strategy_exponent = 2.0;
  auto invalid_production_delay = config(gtosd::SolverAlgorithm::ProductionDcfr, 32);
  invalid_production_delay.averaging_delay = 1U;
  require(!gtosd::solve_finite_game(kuhn, invalid_production_threads),
          "ProductionDcfr rejects a non-contract worker count");
  require(!gtosd::solve_finite_game(kuhn, invalid_production_gamma),
          "ProductionDcfr rejects a non-contract discount schedule");
  require(!gtosd::solve_finite_game(kuhn, invalid_production_delay),
          "ProductionDcfr rejects delayed averaging");

  auto legacy_checkpoint = first_half.value().checkpoint;
  legacy_checkpoint.minor = 0U;
  const auto legacy_serialized = gtosd::serialize_solver_checkpoint(legacy_checkpoint);
  const auto legacy_restored =
      legacy_serialized ? gtosd::deserialize_solver_checkpoint(legacy_serialized.value())
                        : gtosd::Result<gtosd::SolverCheckpoint, gtosd::SolverError>::failure(
                              gtosd::SolverError::InvalidCheckpoint);
  require(legacy_restored.has_value() && legacy_restored.value().minor == 0U,
          "legacy generic checkpoint 1.0 remains readable");

  auto sampled_parallel = config(gtosd::SolverAlgorithm::ExternalSamplingMccfr, 100);
  sampled_parallel.thread_count = 2;
  const auto sampled_rejected = gtosd::solve_finite_game(kuhn, sampled_parallel);
  require(!sampled_rejected && sampled_rejected.error() == gtosd::SolverError::InvalidConfiguration,
          "sampled traversal rejects nondeterministic parallel mode");
}

void test_policy_completion_and_coverage_audit() {
  const auto game = gtosd::make_matching_pennies_game();
  const auto uniform = gtosd::uniform_strategy_profile(game);
  require(uniform.has_value(), "matching policy fixture is available");

  auto partial = uniform.value();
  partial.erase("matching:p1");
  partial.emplace("foreign:unused", gtosd::InformationSetStrategy{0U, {0U, 1U}, {0.25, 0.75}});
  const auto completed =
      gtosd::complete_strategy_profile(game, partial, gtosd::PolicyCompletionRule::UniformUnseenV1);
  require(completed.has_value(), "uniform unseen completion accepts a partial portable policy");
  const auto &audit = completed.value().coverage;
  require(std::string{gtosd::policy_completion_rule_name(audit.completion_rule)} ==
                  "uniform_unseen_v1" &&
              audit.target_game_fingerprint == gtosd::finite_game_fingerprint(game),
          "coverage audit records the versioned completion contract and target game");
  require(audit.supplied_information_sets == 2U && audit.required_information_sets == 2U &&
              audit.matched_information_sets == 1U && audit.unseen_information_sets == 1U &&
              audit.unused_supplied_information_sets == 1U,
          "coverage audit separates required, matched, unseen and unused keys");
  require(audit.required_information_sets_by_player == std::array<std::uint64_t, 2>{1U, 1U} &&
              audit.unseen_information_sets_by_player == std::array<std::uint64_t, 2>{0U, 1U},
          "coverage audit attributes missing information sets to the acting player");
  require(audit.decision_nodes == 3U && audit.unseen_decision_nodes == 2U,
          "coverage audit counts every physical decision node sharing the missing key");
  require_near(audit.exact_key_coverage, 0.5, 1.0e-12,
               "exact-key coverage is normalized by required information sets");
  require_near(audit.total_decision_reach_mass, 2.0, 1.0e-12,
               "decision reach mass equals expected decision visits");
  require_near(audit.unseen_decision_reach_mass, 1.0, 1.0e-12,
               "unseen reach mass includes both opponent nodes");
  require_near(audit.reach_weighted_coverage, 0.5, 1.0e-12,
               "reach-weighted coverage excludes the completed opponent key");
  require_near(audit.reach_weighted_coverage_by_player[0], 1.0, 1.0e-12,
               "known player policy has full reach coverage");
  require_near(audit.reach_weighted_coverage_by_player[1], 0.0, 1.0e-12,
               "missing player policy has zero reach coverage");
  require(gtosd::validate_strategy_profile(game, completed.value().profile).has_value(),
          "completed policy is a complete valid strategy profile");
  const auto completed_value = gtosd::evaluate_strategy_profile(game, completed.value().profile);
  const auto uniform_value = gtosd::evaluate_strategy_profile(game, uniform.value());
  require(completed_value.has_value() && uniform_value.has_value() &&
              completed_value.value() == uniform_value.value(),
          "explicit uniform completion has the expected profile value");

  const auto rejected =
      gtosd::complete_strategy_profile(game, partial, gtosd::PolicyCompletionRule::RejectMissing);
  require(!rejected && rejected.error() == gtosd::SolverError::InvalidStrategy,
          "reject-missing completion refuses an incomplete policy");

  auto malformed = partial;
  malformed.at("foreign:unused").probabilities = {0.25, 0.25};
  const auto malformed_result = gtosd::complete_strategy_profile(
      game, malformed, gtosd::PolicyCompletionRule::UniformUnseenV1);
  require(!malformed_result && malformed_result.error() == gtosd::SolverError::InvalidStrategy,
          "malformed unused policy entries are rejected instead of ignored");

  const auto unknown_rule = gtosd::complete_strategy_profile(
      game, partial, static_cast<gtosd::PolicyCompletionRule>(255U));
  require(!unknown_rule && unknown_rule.error() == gtosd::SolverError::InvalidConfiguration,
          "unknown policy completion contracts are rejected explicitly");

  const auto complete_audit = gtosd::complete_strategy_profile(
      game, uniform.value(), gtosd::PolicyCompletionRule::UniformUnseenV1);
  require(complete_audit.has_value() && complete_audit.value().coverage.exact_key_coverage == 1.0 &&
              complete_audit.value().coverage.reach_weighted_coverage == 1.0 &&
              complete_audit.value().coverage.unseen_information_sets == 0U,
          "a complete profile reports full exact-key and reach coverage");
}

void test_rake_general_sum_and_leduc_smoke() {
  const auto rake_game_result = gtosd::make_short_deck_river_toy_game(0.05);
  require(rake_game_result.has_value(), "valid rake toy builds");
  const auto &rake_game = rake_game_result.value();
  const auto invalid_rake = gtosd::make_short_deck_river_toy_game(0.5);
  require(!invalid_rake && invalid_rake.error() == gtosd::SolverError::InvalidConfiguration,
          "invalid toy rake rejected instead of disabled silently");
  const auto uniform = gtosd::uniform_strategy_profile(rake_game);
  require(uniform.has_value(), "rake toy uniform strategy available");
  const auto metrics = gtosd::calculate_nash_conv(rake_game, uniform.value());
  require(metrics.has_value(), "rake toy NashConv evaluates");
  require(metrics.value().expected_payoff_sum < 0.0, "rake toy is general-sum negative");
  require(std::isnan(metrics.value().zero_sum_exploitability),
          "zero-sum exploitability not invented for rake game");
  require(metrics.value().nash_conv >= 0.0, "general-sum NashConv is non-negative");

  const auto solved =
      gtosd::solve_finite_game(rake_game, config(gtosd::SolverAlgorithm::Dcfr, 20'000));
  require(solved.has_value(), "DCFR runs on Short Deck rake toy");
  const auto solved_metrics =
      gtosd::calculate_nash_conv(rake_game, solved.value().average_strategy);
  require(solved_metrics.has_value() &&
              solved_metrics.value().nash_conv < metrics.value().nash_conv,
          "DCFR reduces rake-toy NashConv without zero-sum assumptions");

  const auto leduc = gtosd::make_leduc_poker_game();
  const auto leduc_run =
      gtosd::solve_finite_game(leduc, config(gtosd::SolverAlgorithm::VanillaCfr, 25));
  require(leduc_run.has_value(), "full traversal handles Leduc public chance and two streets");
  const auto leduc_value =
      gtosd::evaluate_strategy_profile(leduc, leduc_run.value().average_strategy);
  require(leduc_value.has_value(), "Leduc average strategy evaluates exactly");
  require_near(leduc_value.value()[0] + leduc_value.value()[1], 0.0, 1.0e-10,
               "rake-free Leduc remains zero-sum");
}

} // namespace

int main() {
  try {
    test_reference_game_contracts();
    test_reference_equilibria_and_infoset_aware_best_response();
    test_exact_solver_variants();
    test_checkpoint_resume_and_validation();
    test_policy_completion_and_coverage_audit();
    test_rake_general_sum_and_leduc_smoke();
    std::cout << "F5_SOLVER_LAB_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "reference_games=4\n"
              << "algorithms=5\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F5_SOLVER_LAB_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "F5_SOLVER_LAB_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
