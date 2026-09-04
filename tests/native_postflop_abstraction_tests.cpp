#include "gtosd/postflop/canonical_layout.hpp"
#include "gtosd/postflop/postflop_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
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

gtosd::PostflopTreeConfig make_river_config() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  config.flop = {gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value(),
                 gtosd::parse_card("Qh").value()};
  config.turn = gtosd::parse_card("8s").value();
  config.river = gtosd::parse_card("9d").value();
  config.initial_pot = gtosd::Money::from_antes(20).value();
  config.effective_stack = gtosd::Money::from_antes(40).value();
  config.rake.enabled = false;
  const auto half_pot = gtosd::PotPercentage::from_basis_points(5'000).value();
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.aggressive_sizes.clear();
        scenario.raise_depth = 0U;
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
      }
      player[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = {half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes = {
          half_pot};
    }
  }
  return config;
}

gtosd::PostflopRanges make_narrow_ranges(const gtosd::PostflopTreeConfig &config) {
  gtosd::PostflopRanges ranges;
  const auto zero = gtosd::RangeWeight::from_basis_points(0U).value();
  const auto half = gtosd::RangeWeight::from_basis_points(5'000U).value();
  const auto three_quarters = gtosd::RangeWeight::from_basis_points(7'500U).value();
  const auto full = gtosd::RangeWeight::from_basis_points(10'000U).value();
  for (auto &range : ranges.players) {
    range.fill(zero);
  }
  const auto board_mask = config.flop[0].mask() | config.flop[1].mask() | config.flop[2].mask() |
                          (config.turn ? config.turn->mask() : 0U) |
                          (config.river ? config.river->mask() : 0U);
  const auto combos = gtosd::all_combos();
  std::vector<gtosd::CardId> available;
  for (const auto card : gtosd::short_deck()) {
    if ((card.mask() & board_mask) == 0U) {
      available.push_back(card);
    }
  }
  std::size_t selected = 0U;
  for (std::size_t card = 0U; card + 1U < available.size() && selected < 12U; card += 2U) {
    const auto found = std::ranges::find_if(combos, [&](const gtosd::Combo &combo) {
      return (combo.first == available[card] && combo.second == available[card + 1U]) ||
             (combo.first == available[card + 1U] && combo.second == available[card]);
    });
    if (found != combos.end()) {
      const auto combo = static_cast<std::size_t>(std::distance(combos.begin(), found));
      ranges.players[0][combo] = selected % 2U == 0U ? full : half;
      ranges.players[1][combo] = selected % 3U == 0U ? three_quarters : full;
      ++selected;
    }
  }
  require(selected == 12U,
          "narrow fixture selects 12 pairwise-compatible asymmetric weighted combos");
  return ranges;
}

gtosd::CardAbstractionConfig make_abstraction(const std::uint32_t buckets = 3U) {
  gtosd::CardAbstractionConfig abstraction;
  abstraction.kind = gtosd::CardAbstractionKind::EquityFeatureKMeans;
  abstraction.buckets_per_partition = buckets;
  abstraction.maximum_iterations = 50U;
  return abstraction;
}

void test_layout_and_exact_boundary() {
  const auto config = make_river_config();
  const auto ranges = make_narrow_ranges(config);
  const auto exact = gtosd::estimate_postflop_layout(config, ranges);
  const auto abstracted =
      gtosd::estimate_postflop_abstracted_layout(config, ranges, make_abstraction());
  const auto repeated =
      gtosd::estimate_postflop_abstracted_layout(config, ranges, make_abstraction());
  require(exact.has_value(),
          std::string("exact native layout builds: ") +
              (exact.has_value() ? "ok" : gtosd::postflop_solver_error_name(exact.error())));
  require(
      abstracted.has_value(),
      std::string("abstracted native layout builds: ") +
          (abstracted.has_value() ? "ok" : gtosd::postflop_solver_error_name(abstracted.error())));
  require(repeated.has_value(), "repeated abstracted native layout builds");
  require(!exact.value().card_abstraction.has_value(), "exact layout remains non-bucketed");
  require(abstracted.value().card_abstraction.has_value(),
          "abstracted layout publishes its manifest summary");
  require(abstracted.value().information_sets < exact.value().information_sets &&
              abstracted.value().actions < exact.value().actions,
          "native bucketing reduces CFR infosets and actions");
  const auto &summary = *abstracted.value().card_abstraction;
  require(summary.metrics.exact_information_sets == exact.value().information_sets &&
              summary.metrics.abstract_information_sets == abstracted.value().information_sets &&
              summary.metrics.compression_ratio > 1.0 && summary.metrics.uses_lossy_bucketing &&
              !summary.fingerprint.empty(),
          "native abstraction metrics describe the actual state reduction");
  require(repeated.value().card_abstraction->fingerprint == summary.fingerprint,
          "native bucket assignment fingerprint is deterministic");

  auto invalid = make_abstraction();
  invalid.feature_schema_id = "unknown";
  require(!gtosd::estimate_postflop_abstracted_layout(config, ranges, invalid),
          "unknown native feature schema is rejected");

  gtosd::PostflopSolveOptions options;
  options.iterations = 1U;
  options.certification_interval = 1U;
  options.algorithm = gtosd::PostflopAlgorithm::CfrPlus;
  options.state_precision = gtosd::PostflopStatePrecision::Float64;
  options.parallel_action_depth = 0U;
  const auto exact_solved = gtosd::solve_postflop_exact(config, ranges, options);
  require(exact_solved.has_value() && !exact_solved.value().card_abstraction.has_value(),
          "top-level exact solve stays on the non-bucketed path");

  const auto exact_prepared = gtosd::prepare_postflop_tree(config, ranges);
  const auto abstracted_prepared =
      gtosd::prepare_postflop_abstracted_tree(config, ranges, make_abstraction());
  require(exact_prepared.has_value() && abstracted_prepared.has_value(),
          "exact and abstracted prepared trees build");
  require(!gtosd::solve_postflop_abstracted(*exact_prepared.value(), options),
          "abstracted prepared solve rejects an exact layout");
  require(!gtosd::solve_postflop_exact(*abstracted_prepared.value(), options),
          "exact prepared solve rejects an abstracted layout");
  require(gtosd::solve_postflop_exact(*exact_prepared.value(), options).has_value() &&
              gtosd::solve_postflop_abstracted(*abstracted_prepared.value(), options).has_value(),
          "each prepared layout is accepted only by its matching solver API");
}

void test_cfr_plus_solve_resume_certify_and_query() {
  const auto config = make_river_config();
  const auto ranges = make_narrow_ranges(config);
  const auto abstraction = make_abstraction();
  const auto feature_cache = gtosd::build_postflop_card_abstraction_feature_cache(config, ranges);
  require(feature_cache.has_value() && feature_cache.value().partition_count > 0U,
          "native exact feature cache builds independently of bucket count");
  const auto repeated_feature_cache =
      gtosd::build_postflop_card_abstraction_feature_cache(config, ranges);
  require(repeated_feature_cache.has_value() &&
              repeated_feature_cache.value() == feature_cache.value(),
          "eight-worker feature cache construction is bit deterministic");
  gtosd::PostflopSolveOptions options;
  options.iterations = 40U;
  options.certification_interval = 40U;
  options.algorithm = gtosd::PostflopAlgorithm::CfrPlus;
  options.state_precision = gtosd::PostflopStatePrecision::Float64;
  options.parallel_action_depth = 0U;

  const auto solved = gtosd::solve_postflop_abstracted(config, ranges, abstraction, options);
  require(solved.has_value() && solved.value().card_abstraction.has_value() &&
              solved.value().convergence.size() == 1U &&
              !solved.value().card_abstraction->reused_feature_cache &&
              solved.value().card_abstraction->feature_cache_fingerprint ==
                  feature_cache.value().fingerprint,
          "native bucketed CFR+ solve builds its exact features with the deterministic "
          "eight-worker cache path");
  const auto cached_solved =
      gtosd::solve_postflop_abstracted(config, ranges, abstraction, feature_cache.value(), options);
  require(cached_solved.has_value() && cached_solved.value().card_abstraction.has_value() &&
              cached_solved.value().card_abstraction->reused_feature_cache &&
              cached_solved.value().card_abstraction->feature_cache_fingerprint ==
                  feature_cache.value().fingerprint &&
              cached_solved.value().checkpoint.game_fingerprint ==
                  solved.value().checkpoint.game_fingerprint &&
              cached_solved.value().checkpoint.cumulative_regret ==
                  solved.value().checkpoint.cumulative_regret &&
              cached_solved.value().checkpoint.cumulative_strategy ==
                  solved.value().checkpoint.cumulative_strategy,
          "cached and direct feature paths produce bit-identical CFR+ state");
  auto parallel_options = options;
  parallel_options.parallel_action_depth = gtosd::production_postflop_parallel_workers;
  const auto parallel_solved = gtosd::solve_postflop_abstracted(
      config, ranges, abstraction, feature_cache.value(), parallel_options);
  require(parallel_solved.has_value() &&
              parallel_solved.value().checkpoint.cumulative_regret ==
                  solved.value().checkpoint.cumulative_regret &&
              parallel_solved.value().checkpoint.cumulative_strategy ==
                  solved.value().checkpoint.cumulative_strategy &&
              parallel_solved.value().convergence.back().normalized_nash_conv ==
                  solved.value().convergence.back().normalized_nash_conv,
          "eight-thread bucketed CFR+ reproduces serial state and exact best response bitwise");
  const auto &certification = solved.value().convergence.back();
  require(std::isfinite(certification.normalized_nash_conv) &&
              certification.normalized_nash_conv >= 0.0,
          "bucketed strategy is measured by finite exact full-game NashConv");
  require(solved.value().checkpoint.action_count == solved.value().actions &&
              solved.value().checkpoint.cumulative_regret.size() == solved.value().actions &&
              solved.value().checkpoint.cumulative_strategy.size() == solved.value().actions,
          "bucketed checkpoint stores only abstract action state");
  const auto serialized = gtosd::serialize_postflop_checkpoint(solved.value().checkpoint);
  require(serialized.has_value(), "bucketed Float64 checkpoint serializes");
  const auto deserialized = gtosd::deserialize_postflop_checkpoint(serialized.value());
  require(
      deserialized.has_value() &&
          deserialized.value().game_fingerprint == solved.value().checkpoint.game_fingerprint &&
          deserialized.value().cumulative_regret == solved.value().checkpoint.cumulative_regret &&
          deserialized.value().cumulative_strategy == solved.value().checkpoint.cumulative_strategy,
      "bucketed checkpoint round-trip preserves identity and state bits");

  const auto recertified = gtosd::certify_postflop_abstracted_checkpoint(
      config, ranges, abstraction, deserialized.value());
  require(recertified.has_value() && std::abs(recertified.value().normalized_nash_conv -
                                              certification.normalized_nash_conv) < 1.0e-12,
          "standalone abstracted certification reproduces exact NashConv");
  require(!gtosd::certify_postflop_checkpoint(config, ranges, solved.value().checkpoint),
          "exact/no-bucket API rejects an abstracted checkpoint");

  const auto public_tree = gtosd::build_public_tree(config);
  require(public_tree.has_value(), "physical query tree builds");
  const auto root = public_tree.value().root;
  std::map<std::uint32_t, std::vector<gtosd::PostflopStrategyQuery>> by_bucket;
  for (std::size_t combo = 0U; combo < ranges.players[0].size(); ++combo) {
    if (ranges.players[0][combo].basis_points() == 0U) {
      continue;
    }
    auto query = gtosd::query_postflop_abstracted_strategy(config, ranges, abstraction,
                                                           solved.value().checkpoint, root,
                                                           static_cast<gtosd::ComboId>(combo));
    require(query.has_value() && query.value().abstraction_bucket.has_value() &&
                query.value().abstraction_bucket_size.has_value(),
            "abstracted query discloses bucket identity and size");
    double sum = 0.0;
    for (const double probability : query.value().probabilities) {
      require(std::isfinite(probability) && probability >= 0.0,
              "queried bucket probability is finite and non-negative");
      sum += probability;
    }
    require(std::abs(sum - 1.0) < 1.0e-12, "queried bucket strategy is normalized");
    by_bucket[*query.value().abstraction_bucket].push_back(std::move(query.value()));
  }
  require(
      std::ranges::any_of(by_bucket, [](const auto &entry) { return entry.second.size() > 1U; }),
      "at least one queried bucket contains multiple exact combos");
  for (const auto &[bucket, members] : by_bucket) {
    static_cast<void>(bucket);
    for (const auto &member : members) {
      require(member.probabilities == members.front().probabilities,
              "all exact combos in one bucket lift the same average strategy");
      require(*member.abstraction_bucket_size == members.size(),
              "query bucket size matches lifted membership");
    }
  }

  gtosd::PostflopSolveOptions partial_options = options;
  partial_options.iterations = 20U;
  partial_options.certification_interval = 20U;
  const auto partial =
      gtosd::solve_postflop_abstracted(config, ranges, abstraction, partial_options);
  require(partial.has_value(), "partial bucketed CFR+ solve completes");
  const auto resumed = gtosd::solve_postflop_abstracted(config, ranges, abstraction, options,
                                                        &partial.value().checkpoint);
  require(resumed.has_value() &&
              resumed.value().checkpoint.cumulative_regret ==
                  solved.value().checkpoint.cumulative_regret &&
              resumed.value().checkpoint.cumulative_strategy ==
                  solved.value().checkpoint.cumulative_strategy,
          "bucketed CFR+ checkpoint resume is bit deterministic");

  auto unsupported = options;
  unsupported.state_precision = gtosd::PostflopStatePrecision::Float32;
  require(!gtosd::solve_postflop_abstracted(config, ranges, abstraction, unsupported),
          "native bucketed path rejects an unqualified state precision");
  unsupported = options;
  unsupported.algorithm = gtosd::PostflopAlgorithm::Dcfr;
  require(!gtosd::solve_postflop_abstracted(config, ranges, abstraction, unsupported),
          "native bucketed path rejects non-CFR+ algorithms");
  unsupported = options;
  unsupported.parallel_action_depth = gtosd::maximum_postflop_solver_threads;
  require(!gtosd::solve_postflop_abstracted(config, ranges, abstraction, unsupported),
          "native bucketed path rejects more than eight total solver threads");

  auto stale_cache = feature_cache.value();
  stale_cache.source_fingerprint += ":stale";
  require(!gtosd::estimate_postflop_abstracted_layout(config, ranges, abstraction, stale_cache),
          "native layout rejects a cache not bound to the exact game and ranges");
}

void test_turn_cache_multi_granularity_sweep() {
  auto config = make_river_config();
  config.river.reset();
  const auto ranges = make_narrow_ranges(config);
  const auto cache = gtosd::build_postflop_card_abstraction_feature_cache(config, ranges);
  require(cache.has_value() && cache.value().partition_count > 2U &&
              cache.value().observations.size() > 12U,
          "turn cache covers the turn root and exact river partitions");
  gtosd::CanonicalLayoutOptions preflight_options;
  preflight_options.card_abstraction_buckets = {12U};
  const auto preflight = gtosd::estimate_canonical_chance_layout(config, ranges, preflight_options);
  require(preflight.has_value() && preflight.value().card_abstraction_preflight.has_value() &&
              preflight.value().card_abstraction_preflight->partition_count ==
                  cache.value().partition_count &&
              preflight.value().card_abstraction_preflight->observation_count ==
                  cache.value().observations.size(),
          "layout-only preflight predicts the exact deduplicated feature-cache shape");

  std::uint64_t previous_actions = 0U;
  double previous_error = std::numeric_limits<double>::infinity();
  for (const std::uint32_t buckets : std::array<std::uint32_t, 5>{1U, 2U, 3U, 6U, 12U}) {
    const auto estimate = gtosd::estimate_postflop_abstracted_layout(
        config, ranges, make_abstraction(buckets), cache.value());
    require(estimate.has_value() && estimate.value().card_abstraction.has_value() &&
                estimate.value().card_abstraction->reused_feature_cache &&
                estimate.value().card_abstraction->config.buckets_per_partition == buckets,
            "each turn granularity consumes the shared exact feature cache");
    require(estimate.value().actions >= previous_actions,
            "abstract state size is monotone across the configured bucket sweep");
    require(estimate.value().card_abstraction->metrics.weighted_mean_squared_error <=
                previous_error + 1.0e-15,
            "weighted feature error does not increase at finer qualified granularity");
    previous_actions = estimate.value().actions;
    previous_error = estimate.value().card_abstraction->metrics.weighted_mean_squared_error;
  }
}

void test_turn_identity_abstraction_matches_exact_oracle() {
  auto config = make_river_config();
  config.river.reset();
  const auto ranges = make_narrow_ranges(config);
  const auto cache = gtosd::build_postflop_card_abstraction_feature_cache(config, ranges);
  require(cache.has_value(), "identity-oracle turn feature cache builds");
  gtosd::PostflopSolveOptions options;
  options.iterations = 200U;
  options.averaging_delay = 20U;
  options.certification_interval = 200U;
  const auto exact = gtosd::solve_postflop_exact(config, ranges, options);
  const auto abstracted = gtosd::solve_postflop_abstracted(config, ranges, make_abstraction(12U),
                                                           cache.value(), options);
  require(exact.has_value() && abstracted.has_value() && !exact.value().convergence.empty() &&
              !abstracted.value().convergence.empty(),
          "exact and identity-bucket turn solves certify");
  require(abstracted.value().card_abstraction.has_value(),
          "identity abstraction publishes metrics");
  const auto &metrics = abstracted.value().card_abstraction->metrics;
  require(std::abs(metrics.compression_ratio - 1.0) < 1.0e-12,
          "identity abstraction has unit compression ratio");
  require(metrics.weighted_mean_squared_error < 1.0e-24,
          "identity abstraction has zero feature error");
  require(abstracted.value().information_sets == exact.value().information_sets &&
              abstracted.value().actions == exact.value().actions,
          "identity abstraction preserves exact state shape");
  const auto &exact_certification = exact.value().convergence.back();
  const auto &abstracted_certification = abstracted.value().convergence.back();
  require(std::abs(exact_certification.normalized_nash_conv -
                   abstracted_certification.normalized_nash_conv) < 1.0e-12 &&
              std::abs(exact_certification.profile_value_antes[0] -
                       abstracted_certification.profile_value_antes[0]) < 1.0e-12 &&
              std::abs(exact_certification.profile_value_antes[1] -
                       abstracted_certification.profile_value_antes[1]) < 1.0e-12,
          "identity bucketing matches the exact combo oracle at certification");
}

} // namespace

int main() {
  try {
    test_layout_and_exact_boundary();
    test_cfr_plus_solve_resume_certify_and_query();
    test_turn_cache_multi_granularity_sweep();
    test_turn_identity_abstraction_matches_exact_oracle();
    std::cout << "Native postflop abstraction tests passed (" << assertions << " assertions).\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Native postflop abstraction test failure: " << error.what() << '\n';
    return 1;
  }
}
