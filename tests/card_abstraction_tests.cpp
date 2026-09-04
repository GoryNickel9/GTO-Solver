#include "gtosd/abstraction/card_abstraction.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"

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

gtosd::CardAbstractionConfig lossy_config(const std::uint32_t buckets) {
  gtosd::CardAbstractionConfig config;
  config.kind = gtosd::CardAbstractionKind::EquityFeatureKMeans;
  config.buckets_per_partition = buckets;
  config.maximum_iterations = 50;
  config.feature_schema_id = "test-equity-vector-l2-v1";
  return config;
}

std::vector<gtosd::CardAbstractionObservation> simple_observations() {
  return {
      {"river:p0:a", "river:board:history:p0", 0, 0, 0, 1.0, {0.0, 0.1}},
      {"river:p0:b", "river:board:history:p0", 0, 1, 0, 0.0, {0.1, 0.1}},
      {"river:p0:c", "river:board:history:p0", 0, 2, 0, 2.0, {0.9, 0.9}},
      {"river:p0:d", "river:board:history:p0", 0, 3, 0, 1.0, {1.0, 0.9}},
  };
}

gtosd::ComboId combo_id(const std::string_view first, const std::string_view second) {
  const auto first_card = gtosd::parse_card(first);
  const auto second_card = gtosd::parse_card(second);
  require(first_card.has_value() && second_card.has_value(), "test cards parse");
  const auto combos = gtosd::all_combos();
  for (std::size_t index = 0; index < combos.size(); ++index) {
    const bool direct =
        combos[index].first == first_card.value() && combos[index].second == second_card.value();
    const bool reversed =
        combos[index].first == second_card.value() && combos[index].second == first_card.value();
    if (direct || reversed) {
      return static_cast<gtosd::ComboId>(index);
    }
  }
  throw std::runtime_error("test combo not found");
}

void test_exact_postflop_feature_generation() {
  std::vector<gtosd::CardId> board;
  for (const std::string_view text : {"As", "Qd", "9c", "8h", "6s"}) {
    const auto parsed = gtosd::parse_card(text);
    require(parsed.has_value(), "river board card parses");
    board.push_back(parsed.value());
  }
  gtosd::PostflopRanges ranges;
  const auto full = gtosd::RangeWeight::from_basis_points(10'000).value();
  const auto half = gtosd::RangeWeight::from_basis_points(5'000).value();
  ranges.players[0][combo_id("7d", "Kc")] = full;
  ranges.players[0][combo_id("7h", "Kd")] = half;
  ranges.players[1][combo_id("Ah", "Qc")] = full;
  const auto observations = gtosd::build_exact_postflop_equity_observations(
      board, ranges, 0, "river:AsQd9c8h6s:p0", "river-root:p0");
  require(observations.has_value() && observations.value().size() == 2U,
          "exact river features cover each live weighted hero combo");
  for (const auto &observation : observations.value()) {
    require(observation.equity_features.size() == 4U,
            "feature schema contains loss tie win and equity");
    const double outcome_sum = observation.equity_features[0] + observation.equity_features[1] +
                               observation.equity_features[2];
    require(std::abs(outcome_sum - 1.0) <= 1.0e-12, "exact W/T/L features are normalized");
    require((observation.public_card_mask &
             (gtosd::all_combos()[observation.combo].first.mask() |
              gtosd::all_combos()[observation.combo].second.mask())) == 0U,
            "generated observations preserve card removal");
  }
  require(observations.value()[0].reach_weight != observations.value()[1].reach_weight,
          "continuous range weights are preserved, not renormalized");

  board.pop_back();
  board.pop_back();
  const auto flop = gtosd::build_exact_postflop_equity_observations(
      board, ranges, 0, "flop:AsQd9c:p0", "flop-root:p0");
  require(flop.has_value() && flop.value().size() == 2U,
          "flop feature generation enumerates two-card runouts exactly");
}

void test_exact_identity_and_card_removal() {
  auto observations = simple_observations();
  gtosd::CardAbstractionConfig exact;
  exact.kind = gtosd::CardAbstractionKind::ExactIdentity;
  const auto abstraction = gtosd::build_card_abstraction(observations, exact);
  require(abstraction.has_value(), "exact identity abstraction builds");
  require(abstraction.value().metrics.exact_information_sets == observations.size(),
          "exact identity counts source information sets");
  require(abstraction.value().metrics.abstract_information_sets == observations.size(),
          "exact identity preserves information-set cardinality");
  require(!abstraction.value().metrics.uses_lossy_bucketing,
          "exact identity is not mislabeled as lossy");
  require(abstraction.value().metrics.weighted_mean_squared_error == 0.0,
          "exact identity has zero abstraction error");
  for (const auto &assignment : abstraction.value().assignments) {
    require(assignment.information_set == assignment.abstract_information_set,
            "exact identity preserves each information-set name");
  }

  const auto serialized = gtosd::serialize_card_abstraction(abstraction.value());
  require(serialized.has_value(), "exact abstraction serializes");
  const auto restored = gtosd::deserialize_card_abstraction(serialized.value());
  require(restored.has_value(), "exact abstraction deserializes");
  require(restored.value().fingerprint == abstraction.value().fingerprint,
          "abstraction round-trip preserves fingerprint");

  const auto combos = gtosd::all_combos();
  observations.front().public_card_mask = combos.front().first.mask();
  const auto blocked = gtosd::build_card_abstraction(observations, exact);
  require(!blocked && blocked.error() == gtosd::CardAbstractionError::InvalidObservation,
          "combo overlapping the public board is rejected");

  exact.major = 2;
  const auto future = gtosd::build_card_abstraction(simple_observations(), exact);
  require(!future && future.error() == gtosd::CardAbstractionError::UnsupportedVersion,
          "unknown abstraction major is rejected");

  exact.major = gtosd::CardAbstractionConfig::format_major;
  exact.kind = static_cast<gtosd::CardAbstractionKind>(255U);
  const auto unknown_kind = gtosd::build_card_abstraction(simple_observations(), exact);
  require(!unknown_kind &&
              unknown_kind.error() == gtosd::CardAbstractionError::InvalidConfiguration,
          "unknown abstraction kind is rejected before bucket arithmetic");
}

void test_deterministic_weighted_bucketing() {
  const auto observations = simple_observations();
  const auto abstraction = gtosd::build_card_abstraction(observations, lossy_config(2));
  require(abstraction.has_value(), "lossy card abstraction builds");
  require(abstraction.value().metrics.abstract_information_sets == 2U,
          "requested per-partition bucket count is materialized");
  require(abstraction.value().metrics.uses_lossy_bucketing,
          "merged private states are explicitly labeled lossy");
  require(std::abs(abstraction.value().metrics.compression_ratio - 2.0) <= 1.0e-12,
          "compression ratio is reported");
  require(abstraction.value().metrics.weighted_mean_squared_error > 0.0,
          "lossy abstraction error is measured");
  require(abstraction.value().assignments[1].reach_weight == 0.0,
          "present zero-weight state remains represented");

  auto reordered = observations;
  std::ranges::reverse(reordered);
  const auto deterministic = gtosd::build_card_abstraction(reordered, lossy_config(2));
  require(deterministic.has_value(), "reordered observations build");
  require(deterministic.value().fingerprint == abstraction.value().fingerprint,
          "semantic fingerprint is independent of input ordering");

  const auto serialized = gtosd::serialize_card_abstraction(abstraction.value());
  require(serialized.has_value(), "lossy abstraction serializes with source features");
  const auto restored = gtosd::deserialize_card_abstraction(serialized.value());
  require(restored.has_value() && restored.value().fingerprint == abstraction.value().fingerprint,
          "lossy abstraction round-trip is authenticated by fingerprint");

  auto tampered = abstraction.value();
  tampered.assignments.front().abstract_information_set += ":tampered";
  const auto rejected_tamper = gtosd::serialize_card_abstraction(tampered);
  require(!rejected_tamper &&
              rejected_tamper.error() == gtosd::CardAbstractionError::InvalidSerializedData,
          "in-memory assignment tampering is rejected before serialization");

  auto incompatible = observations;
  incompatible.back().player = 1;
  const auto rejected = gtosd::build_card_abstraction(incompatible, lossy_config(2));
  require(!rejected && rejected.error() == gtosd::CardAbstractionError::IncompatiblePartition,
          "one partition cannot mix acting players");
}

std::vector<gtosd::CardAbstractionObservation> kuhn_observations() {
  std::vector<gtosd::CardAbstractionObservation> observations;
  const std::vector<std::string> ranks{"J", "Q", "K"};
  const std::vector<double> strength{0.0, 0.5, 1.0};
  std::uint16_t combo = 10;
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

void test_cfr_plus_consumes_abstract_information_sets() {
  const auto exact_game = gtosd::make_kuhn_poker_game();
  const auto abstraction = gtosd::build_card_abstraction(kuhn_observations(), lossy_config(2));
  require(abstraction.has_value(), "Kuhn card abstraction builds");
  const auto abstract_game = gtosd::apply_card_abstraction(exact_game, abstraction.value());
  require(abstract_game.has_value(), "abstraction rewrites solver information sets");
  const auto exact_summary = gtosd::validate_finite_game(exact_game);
  const auto abstract_summary = gtosd::validate_finite_game(abstract_game.value());
  require(exact_summary.has_value() && abstract_summary.has_value(),
          "exact and abstract games validate");
  require(abstract_summary.value().information_sets == 8U &&
              abstract_summary.value().information_sets < exact_summary.value().information_sets,
          "bucketing reduces the CFR state count from twelve to eight");
  require(abstract_summary.value().fingerprint != exact_summary.value().fingerprint,
          "checkpoint game identity includes the abstraction fingerprint");

  gtosd::SolverConfig config;
  config.algorithm = gtosd::SolverAlgorithm::CfrPlus;
  config.iterations = 20'000;
  config.averaging_delay = 100;
  config.seed = 0x4142535452414354ULL;
  const auto solved = gtosd::solve_finite_game(abstract_game.value(), config);
  require(solved.has_value(), "CFR+ solves the materialized abstract game");
  const auto lifted = gtosd::lift_card_abstraction_strategy(exact_game, abstraction.value(),
                                                            solved.value().average_strategy);
  require(lifted.has_value(), "abstract CFR+ policy lifts to exact private states");
  const auto metrics = gtosd::calculate_nash_conv(exact_game, lifted.value());
  const auto uniform = gtosd::uniform_strategy_profile(exact_game);
  const auto uniform_metrics = gtosd::calculate_nash_conv(exact_game, uniform.value());
  require(metrics.has_value() && uniform_metrics.has_value(),
          "lifted and baseline policies receive exact oracle certification");
  require(std::isfinite(metrics.value().nash_conv), "abstract policy NashConv is finite");
  require(metrics.value().nash_conv < uniform_metrics.value().nash_conv,
          "CFR+ on two buckets improves over the uniform exact-game baseline");

  auto incompatible_observations = kuhn_observations();
  incompatible_observations.front().information_set = "missing:information_set";
  const auto valid_but_foreign =
      gtosd::build_card_abstraction(incompatible_observations, lossy_config(2));
  require(valid_but_foreign.has_value(), "foreign abstraction is internally valid");
  const auto incompatible = gtosd::apply_card_abstraction(exact_game, valid_but_foreign.value());
  require(!incompatible && incompatible.error() == gtosd::CardAbstractionError::IncompatibleGame,
          "abstraction referring to an absent information set is rejected");

  auto tampered = abstraction.value();
  tampered.assignments.front().abstract_information_set += ":tampered";
  const auto tampered_apply = gtosd::apply_card_abstraction(exact_game, tampered);
  require(!tampered_apply &&
              tampered_apply.error() == gtosd::CardAbstractionError::InvalidSerializedData,
          "tampered in-memory abstraction cannot rewrite solver information sets");
}

} // namespace

int main() {
  try {
    test_exact_identity_and_card_removal();
    test_exact_postflop_feature_generation();
    test_deterministic_weighted_bucketing();
    test_cfr_plus_consumes_abstract_information_sets();
    std::cout << "CARD_ABSTRACTION_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "cfr_plus_abstract_path=qualified\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CARD_ABSTRACTION_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
