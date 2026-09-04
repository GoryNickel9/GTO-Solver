#include "gtosd/solver/reference_games.hpp"
#include "gtosd/subgame/subgame_solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::SubgameSolveConfig cfr_plus_config(const std::uint64_t iterations) {
  gtosd::SubgameSolveConfig config;
  config.solver.algorithm = gtosd::SolverAlgorithm::CfrPlus;
  config.solver.iterations = iterations;
  config.solver.averaging_delay = iterations > 100U ? 100U : 0U;
  config.solver.seed = 0x53554247414d4554ULL;
  config.safety = gtosd::SubgameSafetyMode::ExactNashConvGuard;
  config.safety_tolerance = 1.0e-12;
  return config;
}

gtosd::CardAbstractionConfig lossy_config(const std::uint32_t buckets) {
  gtosd::CardAbstractionConfig config;
  config.kind = gtosd::CardAbstractionKind::EquityFeatureKMeans;
  config.buckets_per_partition = buckets;
  config.feature_schema_id = "test-equity-strength-l2-v1";
  return config;
}

std::vector<gtosd::CardAbstractionObservation> kuhn_observations() {
  std::vector<gtosd::CardAbstractionObservation> observations;
  const std::vector<std::string> ranks{"J", "Q", "K"};
  const std::vector<double> strength{0.0, 0.5, 1.0};
  std::uint16_t combo = 100;
  for (std::uint8_t player = 0; player < 2U; ++player) {
    const std::vector<std::string> histories =
        player == 0U ? std::vector<std::string>{"", "kb"} : std::vector<std::string>{"k", "b"};
    for (const auto &history : histories) {
      const std::string partition = "kuhn:p" + std::to_string(player) + ":h=" + history;
      for (std::size_t rank = 0; rank < ranks.size(); ++rank) {
        observations.push_back(
            {"kuhn:p" + std::to_string(player) + ":" + ranks[rank] + ":" + history,
             partition,
             player,
             combo++,
             0,
             1.0,
             {strength[rank]}});
      }
    }
  }
  return observations;
}

void set_binary(gtosd::StrategyProfile &profile, const std::string &information_set,
                const double first, const double second) {
  auto &strategy = profile.at(information_set);
  require(strategy.probabilities.size() == 2U, "binary strategy expected");
  strategy.probabilities = {first, second};
}

bool same_profile(const gtosd::StrategyProfile &left, const gtosd::StrategyProfile &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (const auto &[name, strategy] : left) {
    const auto found = right.find(name);
    if (found == right.end() || found->second.player != strategy.player ||
        found->second.actions != strategy.actions ||
        found->second.probabilities != strategy.probabilities) {
      return false;
    }
  }
  return true;
}

void test_reach_weighted_frontier_and_infoset_closure() {
  const auto kuhn = gtosd::make_kuhn_poker_game();
  const auto blueprint = gtosd::reference_equilibrium_strategy(kuhn);
  require(blueprint.has_value(), "Kuhn blueprint is available");
  std::vector<gtosd::GameNodeId> deal_roots;
  for (const auto &edge : kuhn.nodes[kuhn.root].edges) {
    deal_roots.push_back(edge.child);
  }
  const auto roots =
      gtosd::derive_reach_weighted_subgame_roots(kuhn, blueprint.value(), deal_roots);
  require(roots.has_value() && roots.value().size() == 6U,
          "all six private deals form a reach-weighted frontier");
  for (const auto &root : roots.value()) {
    require(std::abs(root.reach_weight - 1.0 / 6.0) <= 1.0e-12,
            "root reach includes the exact chance probability");
  }

  auto reversed_roots = roots.value();
  std::ranges::reverse(reversed_roots);
  auto unsafe_config = cfr_plus_config(10);
  unsafe_config.safety = gtosd::SubgameSafetyMode::UnsafeIsolated;
  const auto forward = gtosd::solve_subgame(kuhn, blueprint.value(), roots.value(), unsafe_config);
  const auto reverse = gtosd::solve_subgame(kuhn, blueprint.value(), reversed_roots, unsafe_config);
  require(forward.has_value() && reverse.has_value() &&
              forward.value().subgame.game_id == reverse.value().subgame.game_id,
          "frontier identity is independent of caller root ordering");

  const std::vector<gtosd::GameNodeId> one_private_deal{deal_roots.front()};
  const auto one_root =
      gtosd::derive_reach_weighted_subgame_roots(kuhn, blueprint.value(), one_private_deal);
  require(!one_root && one_root.error() == gtosd::SubgameError::InformationSetCrossesBoundary,
          "a private-history cut that splits shared infosets is rejected");

  const std::vector<gtosd::GameNodeId> overlapping{kuhn.root, deal_roots.front()};
  const auto overlap =
      gtosd::derive_reach_weighted_subgame_roots(kuhn, blueprint.value(), overlapping);
  require(!overlap && overlap.error() == gtosd::SubgameError::OverlappingRoots,
          "ancestor and descendant cannot both be frontier roots");
}

void test_safe_guard_accepts_improvement() {
  const auto matching = gtosd::make_matching_pennies_game();
  auto blueprint = gtosd::uniform_strategy_profile(matching);
  require(blueprint.has_value(), "matching blueprint builds");
  set_binary(blueprint.value(), "matching:p0", 1.0, 0.0);
  set_binary(blueprint.value(), "matching:p1", 1.0, 0.0);
  const auto baseline = gtosd::calculate_nash_conv(matching, blueprint.value());
  require(baseline.has_value() && baseline.value().nash_conv > 1.0,
          "deliberately pure blueprint is exploitable");

  const std::vector<gtosd::SubgameRoot> roots{{matching.root, 1.0}};
  const auto resolved =
      gtosd::solve_subgame(matching, blueprint.value(), roots, cfr_plus_config(2'000));
  require(resolved.has_value(), "CFR+ resolves the closed full-game subgame");
  require(resolved.value().deployment == gtosd::SubgameDeployment::CandidateAccepted,
          "exact NashConv guard accepts an improvement");
  require(resolved.value().deployed_metrics.has_value() &&
              resolved.value().baseline_metrics.has_value() &&
              resolved.value().deployed_metrics->nash_conv <
                  resolved.value().baseline_metrics->nash_conv,
          "deployed strategy has lower exact full-game NashConv");
  require(resolved.value().replaced_information_sets.size() == 2U,
          "resolver reports the replaced strategy boundary");
  require(resolved.value().solve.checkpoint.config.algorithm == gtosd::SolverAlgorithm::CfrPlus,
          "subgame checkpoint records CFR+ rather than an implicit algorithm");
  require(resolved.value().solve.checkpoint.config.thread_count == 8U,
          "production subgame checkpoint records the eight-thread solver contract");
}

void test_safe_guard_falls_back_to_blueprint() {
  const auto kuhn = gtosd::make_kuhn_poker_game();
  const auto blueprint = gtosd::reference_equilibrium_strategy(kuhn);
  require(blueprint.has_value(), "equilibrium blueprint is available");
  const std::vector<gtosd::SubgameRoot> roots{{kuhn.root, 1.0}};
  auto config = cfr_plus_config(1);
  config.solver.averaging_delay = 0;
  const auto resolved = gtosd::solve_subgame(kuhn, blueprint.value(), roots, config);
  require(resolved.has_value(), "one-iteration candidate is still evaluated");
  require(resolved.value().candidate_metrics.has_value() &&
              resolved.value().baseline_metrics.has_value() &&
              resolved.value().candidate_metrics->nash_conv >
                  resolved.value().baseline_metrics->nash_conv + config.safety_tolerance,
          "under-trained candidate is measurably worse than equilibrium blueprint");
  require(resolved.value().deployment == gtosd::SubgameDeployment::BlueprintFallback,
          "safe guard rejects the worse candidate");
  require(same_profile(resolved.value().deployed_strategy, blueprint.value()),
          "fallback deploys the blueprint byte-for-byte at strategy level");
  require(resolved.value().deployed_metrics.has_value() &&
              std::abs(resolved.value().deployed_metrics->nash_conv -
                       resolved.value().baseline_metrics->nash_conv) <= 1.0e-12,
          "fallback certificate equals the baseline certificate");

  config.safety = gtosd::SubgameSafetyMode::UnsafeIsolated;
  const auto unsafe = gtosd::solve_subgame(kuhn, blueprint.value(), roots, config);
  require(unsafe.has_value() &&
              unsafe.value().deployment == gtosd::SubgameDeployment::CandidateAccepted,
          "unsafe mode is explicit and deploys without the non-regression guard");
  require(!unsafe.value().baseline_metrics.has_value() &&
              !unsafe.value().candidate_metrics.has_value() &&
              !unsafe.value().deployed_metrics.has_value(),
          "unsafe mode skips whole-game best responses instead of publishing false certificates");
}

void test_invalid_boundaries_and_versions() {
  const auto matching = gtosd::make_matching_pennies_game();
  const auto blueprint = gtosd::uniform_strategy_profile(matching);
  require(blueprint.has_value(), "uniform matching profile builds");
  auto config = cfr_plus_config(10);
  config.major = 2;
  const auto future =
      gtosd::solve_subgame(matching, blueprint.value(), {{matching.root, 1.0}}, config);
  require(!future && future.error() == gtosd::SubgameError::UnsupportedVersion,
          "future subgame config major is rejected");

  config = cfr_plus_config(10);
  const auto zero_weight =
      gtosd::solve_subgame(matching, blueprint.value(), {{matching.root, 0.0}}, config);
  require(!zero_weight && zero_weight.error() == gtosd::SubgameError::InvalidFrontier,
          "zero-reach root is rejected instead of silently normalized");

  config.safety = static_cast<gtosd::SubgameSafetyMode>(255U);
  const auto unknown_mode =
      gtosd::solve_subgame(matching, blueprint.value(), {{matching.root, 1.0}}, config);
  require(!unknown_mode && unknown_mode.error() == gtosd::SubgameError::InvalidConfiguration,
          "unknown safety mode is rejected");
}

void test_abstract_resolve_is_guarded_on_exact_game() {
  const auto exact_game = gtosd::make_kuhn_poker_game();
  const auto abstraction = gtosd::build_card_abstraction(kuhn_observations(), lossy_config(2));
  require(abstraction.has_value(), "lossy Kuhn abstraction builds for composed resolving");
  const auto abstract_game = gtosd::apply_card_abstraction(exact_game, abstraction.value());
  require(abstract_game.has_value(), "abstract Kuhn game materializes");
  const auto abstract_blueprint = gtosd::uniform_strategy_profile(abstract_game.value());
  require(abstract_blueprint.has_value(), "abstract blueprint builds");
  const auto roots = gtosd::derive_reach_weighted_subgame_roots(
      abstract_game.value(), abstract_blueprint.value(), {abstract_game.value().root});
  require(roots.has_value(), "abstract root frontier receives blueprint reach");

  const auto resolved =
      gtosd::solve_abstract_subgame(exact_game, abstraction.value(), abstract_blueprint.value(),
                                    roots.value(), cfr_plus_config(20'000));
  require(resolved.has_value(), "abstract CFR+ subgame solve completes");
  require(resolved.value().abstraction.metrics.uses_lossy_bucketing,
          "result discloses lossy card abstraction");
  require(resolved.value().deployment == gtosd::SubgameDeployment::CandidateAccepted,
          "exact-game guard accepts the improved lifted candidate");
  require(resolved.value().exact_deployed_metrics.has_value() &&
              resolved.value().exact_baseline_metrics.has_value() &&
              resolved.value().exact_deployed_metrics->nash_conv <=
                  resolved.value().exact_baseline_metrics->nash_conv + 1.0e-12,
          "deployment comparison is made after lifting to exact private states");
  require(resolved.value().abstract_solve.solve.checkpoint.game_fingerprint ==
              gtosd::finite_game_fingerprint(resolved.value().abstract_solve.subgame),
          "subgame checkpoint binds the abstract game and frontier identity");
}

} // namespace

int main() {
  try {
    test_reach_weighted_frontier_and_infoset_closure();
    test_safe_guard_accepts_improvement();
    test_safe_guard_falls_back_to_blueprint();
    test_invalid_boundaries_and_versions();
    test_abstract_resolve_is_guarded_on_exact_game();
    std::cout << "SUBGAME_SOLVER_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "safe_guard=exact_full_game_nash_conv\n"
              << "minimizer=cfr_plus\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SUBGAME_SOLVER_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
