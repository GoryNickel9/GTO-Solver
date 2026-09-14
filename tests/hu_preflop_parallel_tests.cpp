#include "gtosd/preflop/hu_preflop.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::HuPreflopSolveOptions parallel_options(const gtosd::HuPreflopSamplingAlgorithm algorithm,
                                              const std::uint8_t worker_threads) {
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 48U;
  options.evaluation_deals = 128U;
  options.best_response_iterations = 16U;
  options.best_response_evaluation_deals = 128U;
  options.equity_samples_per_bucket = 4U;
  options.seed = 0x5236'5452'4149'4E01ULL;
  options.partition_seed = 0x5236'5041'5254'0001ULL;
  options.evaluation_seed = 0x5236'4556'414C'0001ULL;
  options.distributional_bucket_capacities = {64U, 256U, 1'024U};
  options.maximum_bucket_cache_entries = 12'000U;
  options.worker_threads = worker_threads;
  options.training_batch_iterations = 4U;
  options.maximum_parallel_updates_per_job = 2'048U;
  options.sampling_algorithm = algorithm;
  options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype;
  return options;
}

void require_same_numeric_result(const gtosd::HuPreflopSolveResult &left,
                                 const gtosd::HuPreflopSolveResult &right,
                                 const std::string_view message) {
  require(left.root_strategy == right.root_strategy && left.root_ev_ante == right.root_ev_ante &&
              left.root_current_strategy == right.root_current_strategy &&
              left.root_cumulative_weighted_regret == right.root_cumulative_weighted_regret &&
              left.root_cumulative_average_weight == right.root_cumulative_average_weight &&
              left.root_information_last_iteration == right.root_information_last_iteration &&
              left.root_action_advantage_diagnostics == right.root_action_advantage_diagnostics &&
              left.root_ev_standard_error_ante == right.root_ev_standard_error_ante &&
              left.root_action_ev_ante == right.root_action_ev_ante &&
              left.root_action_ev_standard_error_ante == right.root_action_ev_standard_error_ante &&
              left.root_action_ev_samples == right.root_action_ev_samples &&
              left.current_profile_root_ev_ante == right.current_profile_root_ev_ante &&
              left.current_profile_root_ev_standard_error_ante ==
                  right.current_profile_root_ev_standard_error_ante &&
              left.current_profile_root_action_ev_ante ==
                  right.current_profile_root_action_ev_ante &&
              left.current_profile_root_action_ev_standard_error_ante ==
                  right.current_profile_root_action_ev_standard_error_ante &&
              left.current_profile_root_action_ev_samples ==
                  right.current_profile_root_action_ev_samples &&
              left.best_response_co_ev_ante == right.best_response_co_ev_ante &&
              left.best_response_btn_ev_ante == right.best_response_btn_ev_ante &&
              left.current_profile_best_response_co_ev_ante ==
                  right.current_profile_best_response_co_ev_ante &&
              left.current_profile_best_response_btn_ev_ante ==
                  right.current_profile_best_response_btn_ev_ante &&
              left.current_profile_sampled_response_lower_bound_ante ==
                  right.current_profile_sampled_response_lower_bound_ante &&
              left.abstract_nashconv_ante == right.abstract_nashconv_ante &&
              left.information_sets == right.information_sets &&
              left.best_response_information_sets == right.best_response_information_sets &&
              left.preflop_blueprint.decisions == right.preflop_blueprint.decisions,
          message);
}

void test_deterministic_parallel_reduction(const gtosd::HuPreflopTree &tree,
                                           const gtosd::HuPreflopSamplingAlgorithm algorithm) {
  const auto one = gtosd::solve_hu_preflop_sampled(tree, parallel_options(algorithm, 1U));
  require(one.has_value(), "single-worker frozen-batch solve completes");
  require(one.value().peak_parallel_updates_per_job > 0U &&
              one.value().peak_parallel_updates_per_job <= 2'048U &&
              one.value().bucket_cache_peak_entries <= 12'000U,
          "worker deltas and caches remain inside their declared bounds");

  for (const auto workers : std::array<std::uint8_t, 3>{2U, 4U, 8U}) {
    const auto parallel =
        gtosd::solve_hu_preflop_sampled(tree, parallel_options(algorithm, workers));
    require(parallel.has_value(), "multi-worker frozen-batch solve completes");
    require_same_numeric_result(one.value(), parallel.value(),
                                "stable job-order reduction is bit-reproducible across workers");
    require(parallel.value().worker_threads == workers &&
                parallel.value().training_batch_iterations == 4U &&
                parallel.value().peak_parallel_updates_per_job <= 2'048U &&
                parallel.value().peak_parallel_scratch_payload_bytes > 0U &&
                parallel.value().peak_parallel_scratch_payload_bytes <=
                    parallel_options(algorithm, workers).maximum_parallel_scratch_bytes &&
                parallel.value().bucket_cache_peak_entries <= 12'000U,
            "parallel execution reports its scheduler and bounded scratch telemetry");
  }

  const auto repeat = gtosd::solve_hu_preflop_sampled(tree, parallel_options(algorithm, 8U));
  require(repeat.has_value(), "deterministic eight-worker repeat completes");
  require_same_numeric_result(one.value(), repeat.value(),
                              "deterministic mode repeats exactly with the same seeds");
}

void test_parallel_contract_rejections(const gtosd::HuPreflopTree &tree) {
  auto invalid = parallel_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling, 9U);
  const auto too_many_workers = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!too_many_workers &&
              too_many_workers.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "more than eight workers is rejected");

  invalid = parallel_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling, 2U);
  invalid.training_batch_iterations = 0U;
  const auto online_multiworker = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!online_multiworker &&
              online_multiworker.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "multi-worker execution requires an explicit frozen batch");

  invalid = parallel_options(gtosd::HuPreflopSamplingAlgorithm::DiscountedMccfr1503, 2U);
  const auto unsupported_dcfr = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!unsupported_dcfr &&
              unsupported_dcfr.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "lazy discounted updates are rejected until batch clock semantics are implemented");

  invalid = parallel_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling, 2U);
  invalid.preflop_refinement_iterations = 8U;
  const auto unsupported_external_refinement =
      gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!unsupported_external_refinement &&
              unsupported_external_refinement.error() ==
                  gtosd::HuPreflopError::InvalidConfiguration,
          "batch refinement is limited to the verified Linear MCCFR clock");

  invalid = parallel_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling, 2U);
  invalid.maximum_parallel_updates_per_job = 1U;
  const auto exhausted = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!exhausted && exhausted.error() == gtosd::HuPreflopError::MemoryFailure,
          "worker sparse-delta exhaustion fails explicitly without unbounded growth");

  invalid = parallel_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling, 2U);
  invalid.maximum_parallel_scratch_bytes = 1U;
  const auto scratch_rejected = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!scratch_rejected &&
              scratch_rejected.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "an impossible aggregate parallel scratch budget is rejected before allocation");
}

void test_linear_batch_preflop_refinement_contract(const gtosd::HuPreflopTree &tree) {
  auto base_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  base_options.iterations = 24U;
  base_options.export_postflop_policy = true;
  base_options.maximum_exported_postflop_policy_payload_bytes = 64ULL * 1024ULL * 1024ULL;
  base_options.root_action_value_rollouts = 4U;
  base_options.root_continuation_mean_updates = true;
  base_options.symmetric_traverser_mean_updates = true;
  base_options.root_common_random_numbers = true;
  base_options.maximum_parallel_updates_per_job = 10'000U;

  auto refined_options = base_options;
  refined_options.preflop_refinement_iterations = 24U;
  const auto base = gtosd::solve_hu_preflop_sampled(tree, base_options);
  const auto refined_one = gtosd::solve_hu_preflop_sampled(tree, refined_options);
  require(base.has_value() && refined_one.has_value(),
          "Linear frozen-batch training can continue with sequential preflop refinement");
  require(refined_one.value().iterations == 48U &&
              refined_one.value().postflop_training_iterations == 24U &&
              refined_one.value().preflop_refinement_iterations == 24U &&
              refined_one.value().algorithm_id.find("frozen_postflop_rollout_refinement_v1") !=
                  std::string::npos,
          "refinement reports separate postflop and preflop clocks");
  require(base.value().postflop_policy.entries == refined_one.value().postflop_policy.entries,
          "preflop refinement does not mutate average or current postflop state");
  require(base.value().preflop_blueprint.decisions !=
              refined_one.value().preflop_blueprint.decisions,
          "preflop refinement updates the exported preflop strategy");

  auto refined_eight_options = refined_options;
  refined_eight_options.worker_threads = 8U;
  const auto refined_eight =
      gtosd::solve_hu_preflop_sampled(tree, refined_eight_options);
  require(refined_eight.has_value(), "eight-worker base plus refinement completes");
  require_same_numeric_result(
      refined_one.value(), refined_eight.value(),
      "sequential refinement is bit-reproducible after one- and eight-worker bases");
  require(refined_one.value().postflop_policy.entries ==
              refined_eight.value().postflop_policy.entries,
          "postflop state remains bit-identical across worker counts after refinement");
}

void test_unbiased_baseline_parallel_contract(const gtosd::HuPreflopTree &tree) {
  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling, 1U);
  one_options.use_opponent_value_baseline = true;
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;
  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value(),
          "opponent-action baseline completes with one and eight workers");
  require_same_numeric_result(one.value(), eight.value(),
                              "frozen baselines preserve deterministic worker reduction");
  require(one.value().variance_baseline_information_sets > 0U &&
              one.value().variance_baseline_payload_bytes > 0U &&
              one.value().variance_baseline_payload_bytes <=
                  one_options.maximum_variance_baseline_bytes,
          "variance baseline state is observable and bounded");

  auto invalid = one_options;
  invalid.training_batch_iterations = 0U;
  const auto unfrozen = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!unfrozen && unfrozen.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "variance baseline requires an explicit frozen-batch contract");

  invalid = one_options;
  invalid.maximum_variance_baseline_bytes = 256U;
  const auto baseline_exhausted = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!baseline_exhausted && baseline_exhausted.error() == gtosd::HuPreflopError::MemoryFailure,
          "variance baseline exhaustion fails explicitly without dropping observations");
}

void test_public_board_stratification(const gtosd::HuPreflopTree &tree) {
  const std::array<gtosd::CardId, 5> board{
      gtosd::CardId::from_parts(gtosd::Rank::Six, gtosd::Suit::Clubs),
      gtosd::CardId::from_parts(gtosd::Rank::Seven, gtosd::Suit::Diamonds),
      gtosd::CardId::from_parts(gtosd::Rank::Eight, gtosd::Suit::Hearts),
      gtosd::CardId::from_parts(gtosd::Rank::Nine, gtosd::Suit::Spades),
      gtosd::CardId::from_parts(gtosd::Rank::Ten, gtosd::Suit::Clubs)};
  const auto samples = gtosd::sample_hu_preflop_public_board_private_deals(
      board, 0U, 465U, 0x5236'5043'5300'0001ULL);
  require(samples.has_value() && samples.value().size() == 465U,
          "public-board sampler enumerates one complete traverser stratum");
  std::set<std::uint64_t> own_holes;
  std::uint64_t sampled_card_value_sum = 0U;
  for (const auto &sample : samples.value()) {
    const auto own_mask = sample.holes[0][0].mask() | sample.holes[0][1].mask();
    const auto opponent_mask = sample.holes[1][0].mask() | sample.holes[1][1].mask();
    std::uint64_t board_mask = 0U;
    for (const auto card : board) {
      board_mask |= card.mask();
    }
    require((own_mask & opponent_mask) == 0U && ((own_mask | opponent_mask) & board_mask) == 0U,
            "every stratified physical deal is collision free");
    own_holes.insert(own_mask);
    sampled_card_value_sum += sample.holes[0][0].value() + sample.holes[0][1].value();
  }
  std::uint64_t exact_card_value_sum = 0U;
  const auto deck = gtosd::short_deck();
  for (std::size_t first = 0U; first + 1U < deck.size(); ++first) {
    if (std::ranges::find(board, deck[first]) != board.end()) {
      continue;
    }
    for (std::size_t second = first + 1U; second < deck.size(); ++second) {
      if (std::ranges::find(board, deck[second]) == board.end()) {
        exact_card_value_sum += deck[first].value() + deck[second].value();
      }
    }
  }
  require(own_holes.size() == 465U && sampled_card_value_sum == exact_card_value_sum,
          "a complete stratum matches exact private-combination enumeration");

  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling, 1U);
  one_options.chance_sampling_mode = gtosd::HuPreflopChanceSamplingMode::PublicBoardStratified;
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;
  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value(),
          "public-board stratified training completes with one and eight workers");
  require_same_numeric_result(one.value(), eight.value(),
                              "public-board batches remain bit-reproducible across workers");
  require(one.value().chance_sampling_id == "public_board_stratified_physical_deal_v1" &&
              one.value().algorithm_id.find("public_board_stratified_v1") != std::string::npos,
          "result records the public-chance sampling contract");

  auto invalid = one_options;
  invalid.training_batch_iterations = 466U;
  const auto oversized = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!oversized && oversized.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "a batch larger than the complete private stratum is rejected");
}

void test_root_conditional_sampler() {
  std::array<std::uint32_t, gtosd::hu_preflop_hand_class_count> physical_class_counts{};
  const auto deck = gtosd::short_deck();
  for (std::size_t first = 0U; first + 1U < deck.size(); ++first) {
    for (std::size_t second = first + 1U; second < deck.size(); ++second) {
      ++physical_class_counts[gtosd::hand_class(gtosd::Combo{deck[first], deck[second]})];
    }
  }
  const auto physical_combo_count =
      std::accumulate(physical_class_counts.begin(), physical_class_counts.end(), 0U);
  require(physical_combo_count == 630U, "root class masses enumerate all physical CO combos");
  for (const auto count : physical_class_counts) {
    const auto joint_probability =
        (static_cast<double>(count) / physical_combo_count) / static_cast<double>(count);
    require(std::abs(joint_probability - 1.0 / 630.0) < 1.0e-15,
            "class-mass times conditional-combo mass is uniform over 630 combos");
  }
  constexpr std::uint8_t rollout_count = 4U;
  const auto rollout_weight = 1.0 / static_cast<double>(rollout_count);
  require(std::abs(rollout_count * rollout_weight - 1.0) < 1.0e-15,
          "continuation rollout weights sum to one update");

  for (std::uint8_t traverser = 0U; traverser < 2U; ++traverser) {
    for (std::uint8_t class_id = 0U; class_id < gtosd::hu_preflop_hand_class_count; ++class_id) {
      const auto seed = 0x5236'434F'4E44'0001ULL + class_id + 256U * traverser;
      const auto first =
          gtosd::sample_hu_preflop_root_conditional_deals(class_id, traverser, 4U, seed);
      const auto repeated =
          gtosd::sample_hu_preflop_root_conditional_deals(class_id, traverser, 4U, seed);
      require(first.has_value() && repeated.has_value() && first.value() == repeated.value(),
              "root-conditional deals are deterministic for every class and traverser");
      for (const auto &deal : first.value()) {
        require(gtosd::hand_class(
                    gtosd::Combo{deal.holes[traverser][0], deal.holes[traverser][1]}) == class_id,
                "root-conditional deal preserves the requested traverser class");
        std::set<gtosd::CardId> cards;
        cards.insert(deal.holes[0].begin(), deal.holes[0].end());
        cards.insert(deal.holes[1].begin(), deal.holes[1].end());
        cards.insert(deal.board.begin(), deal.board.end());
        require(cards.size() == 9U, "root-conditional physical deal has no card collision");
      }
    }
  }
  const auto invalid = gtosd::sample_hu_preflop_root_conditional_deals(81U, 0U, 1U, 1U);
  require(!invalid && invalid.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "invalid root-conditional class is rejected");
}

void test_root_multi_rollout_contract(const gtosd::HuPreflopTree &tree) {
  auto primary_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  primary_options.iterations = 1U;
  primary_options.export_postflop_policy = true;
  primary_options.maximum_exported_postflop_policy_payload_bytes = 64ULL * 1024ULL * 1024ULL;
  auto multi_options = primary_options;
  multi_options.root_action_value_rollouts = 4U;
  const auto primary = gtosd::solve_hu_preflop_sampled(tree, primary_options);
  const auto multi = gtosd::solve_hu_preflop_sampled(tree, multi_options);
  require(primary.has_value() && multi.has_value(),
          "one-iteration primary and root multi-rollout solves complete");
  require(primary.value().postflop_policy.entries == multi.value().postflop_policy.entries,
          "discarded root shadow traversals do not update postflop training state");
  require(multi.value().root_action_value_rollouts == 4U &&
              !multi.value().root_continuation_mean_updates &&
              multi.value().peak_parallel_shadow_updates_per_worker > 0U &&
              multi.value().algorithm_id.find("root_action_rollouts4_v1") != std::string::npos,
          "root multi-rollout result identifies its bounded shadow work");

  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  one_options.root_action_value_rollouts = 4U;
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;
  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value(),
          "root multi-rollout solve completes with one and eight workers");
  require_same_numeric_result(one.value(), eight.value(),
                              "root multi-rollout reduction is bit-reproducible across workers");

  auto trained_options = multi_options;
  trained_options.root_continuation_mean_updates = true;
  trained_options.maximum_parallel_updates_per_job = 10'000U;
  const auto trained = gtosd::solve_hu_preflop_sampled(tree, trained_options);
  require(trained.has_value(), "root continuation-mean solve completes");
  require(trained.value().postflop_policy.entries != multi.value().postflop_policy.entries,
          "root continuation-mean mode reduces shadow updates into postflop training state");
  require(trained.value().root_continuation_mean_updates &&
              trained.value().algorithm_id.find("continuation_mean_updates_v1") !=
                  std::string::npos,
          "root continuation-mean result records its distinct update contract");

  auto trained_one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  trained_one_options.root_action_value_rollouts = 4U;
  trained_one_options.root_continuation_mean_updates = true;
  trained_one_options.maximum_parallel_updates_per_job = 10'000U;
  auto trained_eight_options = trained_one_options;
  trained_eight_options.worker_threads = 8U;
  const auto trained_one = gtosd::solve_hu_preflop_sampled(tree, trained_one_options);
  const auto trained_eight = gtosd::solve_hu_preflop_sampled(tree, trained_eight_options);
  require(trained_one.has_value() && trained_eight.has_value(),
          "root continuation-mean solve completes with one and eight workers");
  require_same_numeric_result(trained_one.value(), trained_eight.value(),
                              "root continuation-mean reduction is bit-reproducible across workers");

  auto invalid = multi_options;
  invalid.root_action_value_rollouts = 0U;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "zero root action-value rollouts are rejected");
  invalid.root_action_value_rollouts = 9U;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "more than eight root action-value rollouts are rejected");
  invalid.root_action_value_rollouts = 4U;
  invalid.training_batch_iterations = 0U;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "root multi-rollout requires a frozen batch");
  invalid.training_batch_iterations = 4U;
  invalid.chance_sampling_mode = gtosd::HuPreflopChanceSamplingMode::PublicBoardStratified;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "root multi-rollout rejects incompatible public-board sampling");

  invalid = multi_options;
  invalid.root_action_value_rollouts = 1U;
  invalid.root_continuation_mean_updates = true;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "continuation-mean updates require multiple rollouts");
  invalid = multi_options;
  invalid.root_continuation_mean_updates = true;
  invalid.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ExternalSampling;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "continuation-mean updates require the frozen Linear MCCFR contract");
  invalid = multi_options;
  invalid.root_continuation_mean_updates = true;
  invalid.use_opponent_value_baseline = true;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "continuation-mean updates reject the unweighted opponent baseline accumulator");
}

void test_symmetric_traverser_mean_contract(const gtosd::HuPreflopTree &tree) {
  auto co_only_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  co_only_options.iterations = 1U;
  co_only_options.root_action_value_rollouts = 4U;
  co_only_options.root_continuation_mean_updates = true;
  co_only_options.maximum_parallel_updates_per_job = 10'000U;
  co_only_options.export_postflop_policy = true;
  co_only_options.maximum_exported_postflop_policy_payload_bytes = 64ULL * 1024ULL * 1024ULL;
  auto symmetric_options = co_only_options;
  symmetric_options.symmetric_traverser_mean_updates = true;
  const auto co_only = gtosd::solve_hu_preflop_sampled(tree, co_only_options);
  const auto symmetric = gtosd::solve_hu_preflop_sampled(tree, symmetric_options);
  require(co_only.has_value() && symmetric.has_value(),
          "CO-only and symmetric traverser-mean solves complete");
  require(symmetric.value().postflop_policy.entries != co_only.value().postflop_policy.entries,
          "symmetric traverser-mean mode reduces additional BTN updates into the blueprint");
  require(symmetric.value().symmetric_traverser_mean_updates &&
              symmetric.value().algorithm_id.find("symmetric_traverser_mean_updates_v1") !=
                  std::string::npos,
          "symmetric traverser-mean result records its distinct update contract");

  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  one_options.root_action_value_rollouts = 4U;
  one_options.root_continuation_mean_updates = true;
  one_options.symmetric_traverser_mean_updates = true;
  one_options.maximum_parallel_updates_per_job = 10'000U;
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;
  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value(),
          "symmetric traverser-mean solve completes with one and eight workers");
  require_same_numeric_result(one.value(), eight.value(),
                              "symmetric traverser-mean reduction is bit-reproducible across workers");

  auto invalid = co_only_options;
  invalid.root_continuation_mean_updates = false;
  invalid.symmetric_traverser_mean_updates = true;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "symmetric traverser-mean updates require CO continuation-mean updates");
}

void test_root_common_random_numbers_contract(const gtosd::HuPreflopTree &tree) {
  auto independent_options =
      parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  independent_options.iterations = 8U;
  independent_options.root_action_value_rollouts = 4U;
  independent_options.root_continuation_mean_updates = true;
  independent_options.symmetric_traverser_mean_updates = true;
  independent_options.maximum_parallel_updates_per_job = 10'000U;
  auto common_options = independent_options;
  common_options.root_common_random_numbers = true;

  const auto independent = gtosd::solve_hu_preflop_sampled(tree, independent_options);
  const auto common = gtosd::solve_hu_preflop_sampled(tree, common_options);
  require(independent.has_value() && common.has_value(),
          "independent and common-random-number root solves complete");
  require(common.value().root_common_random_numbers &&
              common.value().algorithm_id.find("root_common_random_numbers_v1") !=
                  std::string::npos,
          "common-random-number solve records its distinct estimator contract");
  require(common.value().root_cumulative_weighted_regret !=
              independent.value().root_cumulative_weighted_regret,
          "common random numbers change the joint root estimator");

  auto one_worker = common_options;
  one_worker.worker_threads = 1U;
  auto eight_workers = common_options;
  eight_workers.worker_threads = 8U;
  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_worker);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_workers);
  require(one.has_value() && eight.has_value(),
          "common-random-number solve completes with one and eight workers");
  require_same_numeric_result(one.value(), eight.value(),
                              "common-random-number reduction is bit-reproducible across workers");

  auto invalid = common_options;
  invalid.training_batch_iterations = 0U;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "common random numbers require a frozen deterministic batch");
}

void test_full_current_profile_parallel_determinism(const gtosd::HuPreflopTree &tree) {
  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  one_options.root_action_value_rollouts = 4U;
  one_options.root_continuation_mean_updates = true;
  one_options.symmetric_traverser_mean_updates = true;
  one_options.root_common_random_numbers = true;
  one_options.maximum_parallel_updates_per_job = 10'000U;
  one_options.evaluate_current_profile = true;
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;

  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value() && one.value().current_profile_evaluated &&
              eight.value().current_profile_evaluated,
          "full-current profile completes with one and eight workers");
  require_same_numeric_result(
      one.value(), eight.value(),
      "full-current profile and sampled responses are bit-reproducible across workers");
}

void test_root_first_opponent_response_stratification_contract(
    const gtosd::HuPreflopTree &tree) {
  auto common_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  common_options.iterations = 8U;
  common_options.root_action_value_rollouts = 4U;
  common_options.root_continuation_mean_updates = true;
  common_options.symmetric_traverser_mean_updates = true;
  common_options.root_common_random_numbers = true;
  common_options.maximum_parallel_updates_per_job = 10'000U;
  auto stratified_options = common_options;
  stratified_options.root_first_opponent_response_stratification = true;

  const auto common = gtosd::solve_hu_preflop_sampled(tree, common_options);
  const auto stratified = gtosd::solve_hu_preflop_sampled(tree, stratified_options);
  require(common.has_value() && stratified.has_value(),
          "common and first-response-stratified root solves complete");
  require(stratified.value().root_first_opponent_response_stratification &&
              stratified.value().algorithm_id.find(
                  "root_first_opponent_response_stratification_v1") != std::string::npos,
          "first-response-stratified solve records its distinct estimator contract");
  require(stratified.value().root_cumulative_weighted_regret !=
              common.value().root_cumulative_weighted_regret,
          "first-response strata change the grouped root estimator");

  auto one_worker = stratified_options;
  one_worker.worker_threads = 1U;
  auto eight_workers = stratified_options;
  eight_workers.worker_threads = 8U;
  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_worker);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_workers);
  require(one.has_value() && eight.has_value(),
          "first-response-stratified solve completes with one and eight workers");
  require_same_numeric_result(
      one.value(), eight.value(),
      "first-response-stratified reduction is bit-reproducible across workers");

  auto invalid = stratified_options;
  invalid.root_action_value_rollouts = 1U;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "first-response stratification requires multiple rollout strata");
  invalid = stratified_options;
  invalid.root_continuation_mean_updates = false;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "first-response stratification requires averaged continuation updates");
  invalid = stratified_options;
  invalid.symmetric_traverser_mean_updates = false;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "first-response stratification requires symmetric traverser averaging");
  invalid = stratified_options;
  invalid.training_batch_iterations = 0U;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "first-response stratification requires a frozen deterministic batch");
  invalid = stratified_options;
  invalid.chance_sampling_mode = gtosd::HuPreflopChanceSamplingMode::PublicBoardStratified;
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid).has_value(),
          "first-response stratification rejects incompatible public-board sampling");
}

void test_selective_history_v9_parallel_determinism(const gtosd::HuPreflopTree &tree) {
  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  one_options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9;
  one_options.distributional_bucket_capacities = {32U, 128U, 512U};
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;

  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value(),
          "selective-history-v9 completes with one and eight workers");
  require_same_numeric_result(one.value(), eight.value(),
                              "selective-history-v9 is bit-reproducible across workers");
  require(one.value().abstraction_id.find("selective_history_v9") != std::string::npos,
          "selective-history-v9 parallel result records its abstraction identity");
}

void test_category_history_v10_parallel_determinism(const gtosd::HuPreflopTree &tree) {
  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  one_options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10;
  one_options.distributional_bucket_capacities = {32U, 128U, 512U};
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;

  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value(),
          "category-history-v10 completes with one and eight workers");
  require_same_numeric_result(one.value(), eight.value(),
                              "category-history-v10 is bit-reproducible across workers");
  require(one.value().abstraction_id.find("category_history_v10") != std::string::npos,
          "category-history-v10 parallel result records its abstraction identity");
}

void test_adaptive_category_history_v11_parallel_determinism(
    const gtosd::HuPreflopTree &tree) {
  auto one_options = parallel_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr, 1U);
  one_options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::
      DistributionalStrengthAdaptiveCategoryHistoryV11;
  one_options.distributional_bucket_capacities = {32U, 128U, 512U};
  auto eight_options = one_options;
  eight_options.worker_threads = 8U;

  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_options);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_options);
  require(one.has_value() && eight.has_value(),
          "adaptive-category-history-v11 completes with one and eight workers");
  require_same_numeric_result(
      one.value(), eight.value(),
      "adaptive-category-history-v11 is bit-reproducible across workers");
  require(one.value().abstraction_id.find("category_history_v11") != std::string::npos,
          "adaptive-category-history-v11 parallel result records its abstraction identity");
}

} // namespace

int main() {
  try {
    const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
    require(tree.has_value(), "CO40 tree builds for R6 tests");
    test_deterministic_parallel_reduction(tree.value(),
                                          gtosd::HuPreflopSamplingAlgorithm::ExternalSampling);
    test_deterministic_parallel_reduction(tree.value(),
                                          gtosd::HuPreflopSamplingAlgorithm::LinearMccfr);
    test_parallel_contract_rejections(tree.value());
    test_linear_batch_preflop_refinement_contract(tree.value());
    test_unbiased_baseline_parallel_contract(tree.value());
    test_public_board_stratification(tree.value());
    test_root_conditional_sampler();
    test_root_multi_rollout_contract(tree.value());
    test_symmetric_traverser_mean_contract(tree.value());
    test_root_common_random_numbers_contract(tree.value());
    test_full_current_profile_parallel_determinism(tree.value());
    test_root_first_opponent_response_stratification_contract(tree.value());
    test_selective_history_v9_parallel_determinism(tree.value());
    test_category_history_v10_parallel_determinism(tree.value());
    test_adaptive_category_history_v11_parallel_determinism(tree.value());
    std::cout << "R6_HU_PREFLOP_PARALLEL_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "R6_HU_PREFLOP_PARALLEL_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "R6_HU_PREFLOP_PARALLEL_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
