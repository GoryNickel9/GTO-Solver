#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/card_abstraction.hpp"
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

gtosd::SolverConfig dcfr_config(const std::uint64_t iterations) {
  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::ProductionDcfr;
  config.iterations = iterations;
  config.seed = 0x4142535452414354ULL;
  config.dcfr = {1.5, 0.0, 3.0};
  return config;
}

std::string kuhn_history(const std::string &information_set) {
  const auto last_colon = information_set.rfind(':');
  return last_colon == std::string::npos ? information_set
                                         : information_set.substr(last_colon + 1U);
}

void test_identity_is_an_exact_oracle() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto policy = gtosd::make_identity_card_abstraction(game, "kuhn.identity.v1");
  require(policy.has_value(), "identity policy builds");
  const auto summary = gtosd::validate_card_abstraction_policy(game, policy.value());
  require(summary.has_value() && summary.value().identity, "identity policy validates as identity");
  require(summary.value().perfect_recall_verified,
          "identity is the only automatically verified perfect-recall policy");
  require(summary.value().exact_chance_outcomes,
          "strategy tying never removes physical chance outcomes");

  const auto abstract_game = gtosd::apply_card_abstraction(game, policy.value());
  require(abstract_game.has_value(), "identity abstraction applies");
  auto expected_game = game;
  expected_game.game_id = abstract_game.value().game_id;
  require(gtosd::finite_game_fingerprint(abstract_game.value()) ==
              gtosd::finite_game_fingerprint(expected_game),
          "identity changes only the explicit game identity");

  const auto reference = gtosd::solve_finite_game(game, dcfr_config(8'000));
  const auto abstract = gtosd::solve_finite_game(abstract_game.value(), dcfr_config(8'000));
  require(reference.has_value() && abstract.has_value(), "both identity paths solve");
  const auto lifted =
      gtosd::lift_strategy_profile(game, policy.value(), abstract.value().average_strategy);
  require(lifted.has_value() && lifted.value() == reference.value().average_strategy,
          "identity abstraction lifts to the byte-equal strategy values");

  const auto serialized = gtosd::serialize_card_abstraction_policy(policy.value());
  const auto restored =
      serialized ? gtosd::deserialize_card_abstraction_policy(serialized.value())
                 : gtosd::Result<gtosd::CardAbstractionPolicy, gtosd::SolverError>::failure(
                       gtosd::SolverError::InvalidAbstraction);
  require(restored.has_value() && restored.value().entries == policy.value().entries,
          "policy persistence round-trips");
  require(gtosd::card_abstraction_policy_fingerprint(restored.value()) ==
              summary.value().policy_fingerprint,
          "policy fingerprint survives persistence");
}

void test_weighted_strategy_tying_and_original_game_certification() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto identity = gtosd::make_identity_card_abstraction(game, "kuhn.coarse.v1");
  require(identity.has_value(), "coarse policy starts from complete coverage");
  auto policy = identity.value();
  policy.similarity_metric = "private-rank-agnostic-history";
  policy.perfect_recall = gtosd::PerfectRecallClaim::NotClaimed;
  for (auto &entry : policy.entries) {
    const auto player_end = entry.original_information_set.find(':', 7U);
    const auto player = entry.original_information_set.substr(5U, player_end - 5U);
    const auto history = kuhn_history(entry.original_information_set);
    entry.abstract_information_set = "kuhn_abstract:" + player + ":" + history;
    entry.aggregation_weight =
        entry.original_information_set.find(":K:") != std::string::npos ? 2.0 : 1.0;
  }

  const auto summary = gtosd::validate_card_abstraction_policy(game, policy);
  require(summary.has_value() && !summary.value().identity,
          "coarse policy validates without identity claim");
  require(!summary.value().perfect_recall_verified,
          "non-identity perfect recall is not inferred from names");
  require(summary.value().abstract_information_sets < summary.value().original_information_sets,
          "coarse policy reduces strategic information sets");
  require(summary.value().abstract_action_entries < summary.value().original_action_entries,
          "coarse policy reduces strategic action entries");

  const auto abstract_game = gtosd::apply_card_abstraction(game, policy);
  require(abstract_game.has_value(), "coarse abstraction applies");
  const auto solved = gtosd::solve_finite_game(abstract_game.value(), dcfr_config(20'000));
  require(solved.has_value(), "ProductionDcfr parameters solve the abstract game");
  const auto lifted = gtosd::lift_strategy_profile(game, policy, solved.value().average_strategy);
  require(lifted.has_value(), "abstract strategy lifts to every original information set");
  const auto original_metrics = gtosd::calculate_nash_conv(game, lifted.value());
  require(original_metrics.has_value() && std::isfinite(original_metrics.value().nash_conv),
          "lifted strategy is certified in the original game");
  require(original_metrics.value().nash_conv > 0.1,
          "coarse abstract convergence is not mistaken for original-game quality");

  const auto reference = gtosd::reference_equilibrium_strategy(game);
  const auto aggregated = reference
                              ? gtosd::aggregate_strategy_profile(game, policy, reference.value())
                              : gtosd::Result<gtosd::StrategyProfile, gtosd::SolverError>::failure(
                                    gtosd::SolverError::InvalidStrategy);
  require(aggregated.has_value(), "weighted aggregation creates a normalized strategy");

  const auto bytes = gtosd::estimate_card_abstraction_bytes(game, policy);
  require(bytes.has_value() && bytes.value().minimum_runtime_bytes > 0U &&
              bytes.value().serialized_policy_bytes > bytes.value().minimum_runtime_bytes,
          "byte model separates dense runtime payload from persisted policy");
}

void test_invalid_and_incompatible_policies_are_rejected() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto identity = gtosd::make_identity_card_abstraction(game, "kuhn.invalid.v1");
  require(identity.has_value(), "identity fixture available");

  auto missing = identity.value();
  missing.entries.pop_back();
  require(!gtosd::validate_card_abstraction_policy(game, missing), "partial coverage is rejected");

  auto false_recall = identity.value();
  false_recall.entries.front().abstract_information_set = "merged";
  require(!gtosd::validate_card_abstraction_policy(game, false_recall),
          "non-identity mapping cannot claim identity-verified recall");

  auto incompatible = identity.value();
  incompatible.perfect_recall = gtosd::PerfectRecallClaim::NotClaimed;
  incompatible.entries[0].abstract_information_set = "incompatible";
  incompatible.entries[1].abstract_information_set = "incompatible";
  require(!gtosd::validate_card_abstraction_policy(game, incompatible),
          "different players or action signatures cannot share a bucket");

  auto zero_weight = identity.value();
  zero_weight.entries.front().aggregation_weight = 0.0;
  require(!gtosd::validate_card_abstraction_policy(game, zero_weight),
          "zero aggregation weight is rejected");

  const auto future = gtosd::deserialize_card_abstraction_policy(
      "GTOSD_CARD_ABSTRACTION 99 0\n\"x\"\n\"identity\"\n\"all\"\n"
      "identity_verified\n0\n");
  require(!future && future.error() == gtosd::SolverError::UnsupportedAbstractionVersion,
          "future policy version is rejected explicitly");
}

} // namespace

int main() {
  try {
    test_identity_is_an_exact_oracle();
    test_weighted_strategy_tying_and_original_game_certification();
    test_invalid_and_incompatible_policies_are_rejected();
    std::cout << "CARD_ABSTRACTION_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "oracle=identity\n"
              << "certification=original_game_nash_conv\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CARD_ABSTRACTION_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "CARD_ABSTRACTION_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
