#include "gtosd/equity/evaluator.hpp"
#include "gtosd/preflop/hu_preflop.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
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

gtosd::HuPreflopSolveOptions
options_for(const gtosd::HuPreflopPostflopRepresentation representation,
            const std::array<std::uint16_t, 3> capacities =
                gtosd::hu_preflop_distributional_default_capacities) {
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 64U;
  options.evaluation_deals = 256U;
  options.best_response_iterations = 16U;
  options.best_response_evaluation_deals = 256U;
  options.equity_samples_per_bucket = 4U;
  options.seed = 0x5235'5452'4149'4E01ULL;
  options.partition_seed = 0x5235'5041'5254'0001ULL;
  options.evaluation_seed = 0x5235'4556'414C'0001ULL;
  options.maximum_bucket_cache_entries = 6'000U;
  options.export_postflop_policy = true;
  options.maximum_exported_postflop_policy_payload_bytes = 128ULL * 1024ULL * 1024ULL;
  options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ExternalSampling;
  options.postflop_representation = representation;
  options.distributional_bucket_capacities = capacities;
  return options;
}

void test_mapping_contract() {
  const std::array hole{gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value()};
  const std::array board{gtosd::parse_card("Qh").value(), gtosd::parse_card("9c").value(),
                         gtosd::parse_card("7d").value(), gtosd::parse_card("8s").value(),
                         gtosd::parse_card("6c").value()};
  constexpr std::array<std::uint16_t, 3> coarse{64U, 256U, 1'024U};
  constexpr std::array<std::uint16_t, 3> fine{1'024U, 4'096U, 16'384U};
  constexpr std::array streets{gtosd::Street::Flop, gtosd::Street::Turn, gtosd::Street::River};
  for (std::size_t index = 0U; index < streets.size(); ++index) {
    const auto coarse_bucket = gtosd::compute_hu_preflop_distributional_strength_bucket(
        hole, board, streets[index], 16U, 0x5235'5041'5254'0001ULL, coarse);
    const auto repeated = gtosd::compute_hu_preflop_distributional_strength_bucket(
        hole, board, streets[index], 16U, 0x5235'5041'5254'0001ULL, coarse);
    const auto fine_bucket = gtosd::compute_hu_preflop_distributional_strength_bucket(
        hole, board, streets[index], 16U, 0x5235'5041'5254'0001ULL, fine);
    require(coarse_bucket.has_value() && repeated.has_value() && fine_bucket.has_value() &&
                coarse_bucket.value() == repeated.value(),
            "versioned mapping is deterministic for every capacity tier");
    require(coarse_bucket.value() < coarse[index] && fine_bucket.value() < fine[index],
            "mapping respects the persisted street capacity");
  }

  auto blocked_board = board;
  blocked_board[0] = hole[0];
  const auto blocked = gtosd::compute_hu_preflop_distributional_strength_bucket(
      hole, blocked_board, gtosd::Street::Flop, 16U, 0x5235'5041'5254'0001ULL, coarse);
  require(!blocked && blocked.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "mapping rejects a board blocked by the player's hole cards");
}

void test_profile_v6_uses_lower_distributional_features() {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  constexpr auto partition_seed = 0x5235'5041'5254'0001ULL;
  const auto deck = gtosd::short_deck();
  std::array<int, 32> first_profile_by_legacy_bucket{};
  first_profile_by_legacy_bucket.fill(-1);
  bool split_found = false;
  for (std::size_t first = 0U; first < deck.size() && !split_found; ++first) {
    for (std::size_t second = first + 1U; second < deck.size() && !split_found; ++second) {
      std::array<gtosd::CardId, 5> board{};
      std::size_t board_index = 0U;
      for (std::size_t card = 0U; card < deck.size() && board_index < board.size(); ++card) {
        if (card != first && card != second) {
          board[board_index++] = deck[card];
        }
      }
      const std::array hole{deck[first], deck[second]};
      const auto legacy = gtosd::compute_hu_preflop_distributional_strength_bucket(
          hole, board, gtosd::Street::Flop, 8U, partition_seed, capacities);
      const auto profile = gtosd::compute_hu_preflop_distributional_profile_v6_bucket(
          hole, board, gtosd::Street::Flop, 8U, partition_seed, capacities);
      require(legacy.has_value() && profile.has_value() && profile.value() < capacities[0],
              "profile-v6 mapping is total and bounded on legal Flop observations");
      auto &first_profile = first_profile_by_legacy_bucket[legacy.value()];
      if (first_profile < 0) {
        first_profile = static_cast<int>(profile.value());
      } else if (first_profile != static_cast<int>(profile.value())) {
        split_found = true;
      }
    }
  }
  require(split_found,
          "profile-v6 distinguishes observations collapsed by the legacy top-bit mapping");
}

void test_persisted_mapping_and_coverage(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{64U, 256U, 1'024U};
  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree, options_for(gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype,
                        capacities));
  require(solve.has_value(), "coarse distributional solve completes");
  require(solve.value().postflop_policy.distributional_bucket_capacities == capacities &&
              solve.value().distributional_bucket_capacities == capacities,
          "solve result and policy persist the capacity vector");
  require(solve.value().bucket_mapping_visits[0] > 0U &&
              solve.value().bucket_mapping_computations[0] > 0U &&
              solve.value().bucket_mapping_seconds > 0.0,
          "mapping visits, computations and cost are observable");
  for (std::size_t street = 0U; street < capacities.size(); ++street) {
    require(solve.value().occupied_distributional_buckets[street] <= capacities[street],
            "observed occupancy never exceeds the frozen capacity");
  }
  require(solve.value().average_policy_queries > 0U &&
              solve.value().untrained_average_policy_queries <=
                  solve.value().average_policy_queries,
          "physical self-play reports the mass of missing trained states");

  const auto path = std::filesystem::temp_directory_path() / "gtosd_r5_mapping_policy.bin";
  require(
      gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy, path.string())
          .has_value(),
      "policy with mapping manifest saves atomically");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint &&
              loaded.value().distributional_bucket_capacities == capacities,
          "mapping seed and capacities survive reload under the policy checksum");
  if (!solve.value().postflop_policy.entries.empty()) {
    const auto &entry = solve.value().postflop_policy.entries.front();
    const auto before = gtosd::query_hu_preflop_sampled_postflop_policy(
        solve.value().postflop_policy, entry.key, entry.action_count);
    const auto after = gtosd::query_hu_preflop_sampled_postflop_policy(loaded.value(), entry.key,
                                                                       entry.action_count);
    require(before.has_value() && after.has_value() && before.value() == after.value(),
            "average-policy query is identical after reload");
  }
  std::filesystem::remove(path);
}

void test_capacity_comparison(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> standard{256U, 1'024U, 4'096U};
  constexpr std::array<std::uint16_t, 3> fine{1'024U, 4'096U, 16'384U};
  const auto baseline = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(gtosd::HuPreflopPostflopRepresentation::CategoryEquityMonteCarloMemoryless));
  const auto standard_result = gtosd::solve_hu_preflop_sampled(
      tree, options_for(gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype,
                        standard));
  const auto fine_result = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype, fine));
  require(baseline.has_value() && standard_result.has_value() && fine_result.has_value(),
          "baseline, standard and fine representations complete on the reduced case");
  require(std::isfinite(baseline.value().root_ev_ante) &&
              std::isfinite(standard_result.value().root_ev_ante) &&
              std::isfinite(fine_result.value().root_ev_ante),
          "all capacity candidates receive an independent physical-game EV estimate");
  require(standard_result.value().abstraction_id != fine_result.value().abstraction_id,
          "capacity is part of the versioned abstraction identity");
}

void test_distributional_perfect_recall(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{8U, 32U, 128U};
  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree, options_for(gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPerfectRecall,
                        capacities));
  require(solve.has_value(), "distributional perfect-recall solve completes on the reduced case");
  require(solve.value().abstraction_id.find("hierarchical_feature_perfect_recall_v5") !=
                  std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPerfectRecall,
          "policy identity distinguishes perfect recall from the prototype");
  for (const auto &entry : solve.value().postflop_policy.entries) {
    const auto final_index =
        static_cast<std::size_t>(entry.key.street) - static_cast<std::size_t>(gtosd::Street::Flop);
    require(entry.key.preflop_class < gtosd::hu_preflop_hand_class_count,
            "perfect-recall policy retains a legal preflop class");
    for (std::size_t index = 0U; index <= final_index; ++index) {
      require(entry.key.bucket_history[index] != gtosd::hu_preflop_sampled_postflop_unset_bucket,
              "perfect-recall policy retains every observed street bucket");
    }
  }
  const auto path = std::filesystem::temp_directory_path() / "gtosd_r5_perfect_recall_policy.bin";
  require(
      gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy, path.string())
          .has_value(),
      "perfect-recall policy saves with the current versioned format");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint,
          "perfect-recall policy survives checksum validation and reload");
  std::filesystem::remove(path);
}

void test_distributional_bucket_history(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{8U, 32U, 128U};
  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree, options_for(gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory,
                        capacities));
  require(solve.has_value(), "distributional bucket-history solve completes on the reduced case");
  require(solve.value().abstraction_id.find(
              "hierarchical_feature_bucket_history_imperfect_recall_v5") != std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthBucketHistory,
          "policy identity declares the bucket-history imperfect-recall contract");
  for (const auto &entry : solve.value().postflop_policy.entries) {
    const auto final_index =
        static_cast<std::size_t>(entry.key.street) - static_cast<std::size_t>(gtosd::Street::Flop);
    require(entry.key.preflop_class == 0U,
            "bucket-history policy does not multiply postflop state by preflop class");
    for (std::size_t index = 0U; index <= final_index; ++index) {
      require(entry.key.bucket_history[index] != gtosd::hu_preflop_sampled_postflop_unset_bucket,
              "bucket-history policy retains every observed postflop bucket");
    }
  }
}

void test_distributional_profile_v6(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6,
                  capacities));
  require(solve.has_value(), "distributional profile-v6 solve completes on the reduced case");
  require(solve.value().abstraction_id.find(
              "profile_hash_v6_current_observation_imperfect_recall") != std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6,
          "policy identity distinguishes profile-v6 from the legacy top-bit mapping");
  require(gtosd::validate_hu_preflop_sampled_postflop_policy(
              tree, solve.value().postflop_policy)
              .has_value(),
          "profile-v6 policy passes the persisted-policy validator");
  require(solve.value().postflop_policy.minor ==
              gtosd::HuPreflopSampledPostflopPolicy::minimum_supported_minor &&
              solve.value().postflop_policy.minor == 5U,
          "profile-v6 policy uses sampled-policy format 1.5");
  const auto path = std::filesystem::temp_directory_path() / "gtosd_r6_profile_v6_policy.bin";
  require(gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy,
                                                          path.string())
              .has_value(),
          "profile-v6 policy saves with the versioned format");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() &&
              loaded.value().representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthProfileV6 &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint,
          "profile-v6 policy survives checksum validation and reload");
  std::filesystem::remove(path);
}

void test_distributional_structured_v7_mapping() {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  constexpr auto partition_seed = 0x5235'5041'5254'0001ULL;
  std::array<bool, 9> seen_categories{};
  std::size_t checked = 0U;
  constexpr std::array<std::array<std::string_view, 7>, 7> category_examples{{
      {"Ah", "Kd", "Qc", "Js", "8h", "7d", "6c"},
      {"Ah", "Ad", "Kc", "Qd", "Jh", "8s", "6c"},
      {"Ah", "Ad", "Ac", "Kd", "Qh", "8s", "6c"},
      {"Ah", "Kd", "Qc", "Js", "Th", "7d", "6c"},
      {"Ah", "Kh", "Qh", "9h", "7h", "8s", "6c"},
      {"Ah", "Ad", "Ac", "Kd", "Kh", "8s", "6c"},
      {"Ah", "Ad", "Ac", "As", "Kh", "8s", "6c"},
  }};
  for (const auto &cards : category_examples) {
    const std::array hole{gtosd::parse_card(cards[0]).value(),
                          gtosd::parse_card(cards[1]).value()};
    const std::array board{gtosd::parse_card(cards[2]).value(),
                           gtosd::parse_card(cards[3]).value(),
                           gtosd::parse_card(cards[4]).value(),
                           gtosd::parse_card(cards[5]).value(),
                           gtosd::parse_card(cards[6]).value()};
    const auto bucket = gtosd::compute_hu_preflop_distributional_structured_v7_bucket(
        hole, board, gtosd::Street::River, 8U, partition_seed, capacities);
    const std::array<gtosd::CardId, 7> seven{hole[0], hole[1], board[0], board[1],
                                             board[2], board[3], board[4]};
    const auto value = gtosd::evaluate_seven(seven);
    require(bucket.has_value() && value.has_value() && bucket.value() < capacities[2],
            "structured-v7 mapping is total and bounded on legal River observations");
    const auto category = static_cast<std::size_t>(value.value().category);
    require((bucket.value() >> 5U) == category,
            "structured-v7 reserves a disjoint River stratum for each visible category");
    seen_categories[category] = true;
    ++checked;
  }
  require(checked > 0U &&
              std::ranges::count(seen_categories, true) >= 6,
          "structured-v7 category invariant is exercised across distinct categories");

  const std::array hole{gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value()};
  const std::array board{gtosd::parse_card("Qh").value(), gtosd::parse_card("9c").value(),
                         gtosd::parse_card("7d").value(), gtosd::parse_card("8s").value(),
                         gtosd::parse_card("6c").value()};
  const auto first = gtosd::compute_hu_preflop_distributional_structured_v7_bucket(
      hole, board, gtosd::Street::Turn, 16U, partition_seed, capacities);
  const auto repeated = gtosd::compute_hu_preflop_distributional_structured_v7_bucket(
      hole, board, gtosd::Street::Turn, 16U, partition_seed, capacities);
  require(first.has_value() && repeated.has_value() && first.value() == repeated.value(),
          "structured-v7 mapping is deterministic");

  constexpr std::array<std::uint16_t, 3> invalid_capacities{16U, 128U, 512U};
  const auto invalid = gtosd::compute_hu_preflop_distributional_structured_v7_bucket(
      hole, board, gtosd::Street::Flop, 16U, partition_seed, invalid_capacities);
  require(!invalid && invalid.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "structured-v7 rejects capacities that cannot isolate hand categories");
}

void test_distributional_structured_v7_policy(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7,
                  capacities));
  require(solve.has_value(), "distributional structured-v7 solve completes on the reduced case");
  require(solve.value().abstraction_id.find("category_equity_ordered_profile_v7") !=
                  std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7,
          "policy identity distinguishes structured-v7 from legacy and profile-v6");
  require(solve.value().postflop_policy.minor == 6U &&
              gtosd::validate_hu_preflop_sampled_postflop_policy(
                  tree, solve.value().postflop_policy)
                  .has_value(),
          "structured-v7 uses policy format 1.6 and passes validation");

  const auto path = std::filesystem::temp_directory_path() / "gtosd_r6_structured_v7_policy.bin";
  require(gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy,
                                                          path.string())
              .has_value(),
          "structured-v7 policy saves atomically");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() && loaded.value().minor == 6U &&
              loaded.value().representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7 &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint,
          "structured-v7 policy survives checksum validation and reload");
  std::filesystem::remove(path);

  auto invalid_options = options_for(
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStructuredV7,
      {16U, 128U, 512U});
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid_options),
          "structured-v7 solve rejects a capacity below 32");
}

gtosd::HandCategory visible_category_for_test(const std::array<gtosd::CardId, 2> &hole,
                                              const std::array<gtosd::CardId, 5> &board,
                                              const gtosd::Street street) {
  const std::array<gtosd::CardId, 7> cards{hole[0],  hole[1],  board[0], board[1],
                                           board[2], board[3], board[4]};
  const auto visible_count = street == gtosd::Street::Flop ? 5U
                             : street == gtosd::Street::Turn ? 6U
                                                             : 7U;
  gtosd::HandValue best{};
  bool found = false;
  for (std::size_t first = 0U; first + 4U < visible_count; ++first) {
    for (std::size_t second = first + 1U; second + 3U < visible_count; ++second) {
      for (std::size_t third = second + 1U; third + 2U < visible_count; ++third) {
        for (std::size_t fourth = third + 1U; fourth + 1U < visible_count; ++fourth) {
          for (std::size_t fifth = fourth + 1U; fifth < visible_count; ++fifth) {
            const auto value = gtosd::evaluate_five(
                {cards[first], cards[second], cards[third], cards[fourth], cards[fifth]});
            if (!value) {
              throw std::runtime_error("visible category test received invalid cards");
            }
            if (!found || best < value.value()) {
              best = value.value();
              found = true;
            }
          }
        }
      }
    }
  }
  if (!found) {
    throw std::runtime_error("visible category test did not enumerate a five-card hand");
  }
  return best.category;
}

std::uint64_t street_adaptive_category_group(const gtosd::HandCategory category,
                                             const gtosd::Street street) {
  if (street == gtosd::Street::River) {
    return static_cast<std::uint64_t>(category);
  }
  if (street == gtosd::Street::Turn) {
    return std::min<std::uint64_t>(7U, static_cast<std::uint64_t>(category));
  }
  switch (category) {
  case gtosd::HandCategory::HighCard:
  case gtosd::HandCategory::Pair:
    return 0U;
  case gtosd::HandCategory::TwoPair:
  case gtosd::HandCategory::ThreeOfAKind:
    return 1U;
  case gtosd::HandCategory::Straight:
  case gtosd::HandCategory::Flush:
    return 2U;
  case gtosd::HandCategory::FullHouse:
  case gtosd::HandCategory::FourOfAKind:
  case gtosd::HandCategory::StraightFlush:
    return 3U;
  }
  throw std::runtime_error("unknown hand category");
}

void test_distributional_street_adaptive_v8_mapping() {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  constexpr auto partition_seed = 0x5235'5041'5254'0001ULL;
  constexpr std::array streets{gtosd::Street::Flop, gtosd::Street::Turn,
                               gtosd::Street::River};
  constexpr std::array<std::uint32_t, 3> category_bits{2U, 3U, 4U};
  constexpr std::array<std::array<std::string_view, 7>, 7> category_examples{{
      {"Ah", "Kd", "Qc", "Js", "8h", "7d", "6c"},
      {"Ah", "Ad", "Kc", "Qd", "Jh", "8s", "6c"},
      {"Ah", "Ad", "Ac", "Kd", "Qh", "8s", "6c"},
      {"Ah", "Kd", "Qc", "Js", "Th", "7d", "6c"},
      {"Ah", "Kh", "Qh", "9h", "7h", "8s", "6c"},
      {"Ah", "Ad", "Ac", "Kd", "Kh", "8s", "6c"},
      {"Ah", "Ad", "Ac", "As", "Kh", "8s", "6c"},
  }};
  for (const auto &cards : category_examples) {
    const std::array hole{gtosd::parse_card(cards[0]).value(),
                          gtosd::parse_card(cards[1]).value()};
    const std::array board{gtosd::parse_card(cards[2]).value(),
                           gtosd::parse_card(cards[3]).value(),
                           gtosd::parse_card(cards[4]).value(),
                           gtosd::parse_card(cards[5]).value(),
                           gtosd::parse_card(cards[6]).value()};
    for (std::size_t street_index = 0U; street_index < streets.size(); ++street_index) {
      const auto bucket =
          gtosd::compute_hu_preflop_distributional_street_adaptive_v8_bucket(
              hole, board, streets[street_index], 8U, partition_seed, capacities);
      const auto category = visible_category_for_test(hole, board, streets[street_index]);
      const auto capacity_bits =
          std::countr_zero(static_cast<std::uint32_t>(capacities[street_index]));
      require(bucket.has_value() && bucket.value() < capacities[street_index] &&
                  (bucket.value() >> (capacity_bits - category_bits[street_index])) ==
                      street_adaptive_category_group(category, streets[street_index]),
              "street-adaptive-v8 preserves the frozen category group on every street");
    }
  }

  const std::array hole{gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value()};
  const std::array board{gtosd::parse_card("Qh").value(), gtosd::parse_card("9c").value(),
                         gtosd::parse_card("7d").value(), gtosd::parse_card("8s").value(),
                         gtosd::parse_card("6c").value()};
  const std::array permuted_hole{gtosd::parse_card("As").value(),
                                 gtosd::parse_card("Ks").value()};
  const std::array permuted_board{
      gtosd::parse_card("Qs").value(), gtosd::parse_card("9d").value(),
      gtosd::parse_card("7h").value(), gtosd::parse_card("8c").value(),
      gtosd::parse_card("6d").value()};
  for (const auto street : streets) {
    const auto first = gtosd::compute_hu_preflop_distributional_street_adaptive_v8_bucket(
        hole, board, street, 16U, partition_seed, capacities);
    const auto repeated = gtosd::compute_hu_preflop_distributional_street_adaptive_v8_bucket(
        hole, board, street, 16U, partition_seed, capacities);
    const auto permuted = gtosd::compute_hu_preflop_distributional_street_adaptive_v8_bucket(
        permuted_hole, permuted_board, street, 16U, partition_seed, capacities);
    require(first.has_value() && repeated.has_value() && permuted.has_value() &&
                first.value() == repeated.value() && first.value() == permuted.value(),
            "street-adaptive-v8 is deterministic and globally suit invariant");
  }

  constexpr std::array<std::uint16_t, 3> invalid_capacities{4U, 128U, 512U};
  const auto invalid = gtosd::compute_hu_preflop_distributional_street_adaptive_v8_bucket(
      hole, board, gtosd::Street::Flop, 16U, partition_seed, invalid_capacities);
  require(!invalid && invalid.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "street-adaptive-v8 rejects a capacity with no equity bit");
}

void test_distributional_street_adaptive_v8_policy(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8,
          capacities));
  require(solve.has_value(), "distributional street-adaptive-v8 solve completes");
  require(solve.value().abstraction_id.find("street_adaptive_category_equity_profile_v8") !=
                  std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 &&
              solve.value().postflop_policy.minor == 7U &&
              gtosd::validate_hu_preflop_sampled_postflop_policy(
                  tree, solve.value().postflop_policy)
                  .has_value(),
          "street-adaptive-v8 has a distinct identity and valid policy format 1.7");

  const auto path =
      std::filesystem::temp_directory_path() / "gtosd_r6_street_adaptive_v8_policy.bin";
  require(gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy,
                                                          path.string())
              .has_value(),
          "street-adaptive-v8 policy saves atomically");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() && loaded.value().minor == 7U &&
              loaded.value().representation ==
                  gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8 &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint,
          "street-adaptive-v8 policy survives checksum validation and reload");
  std::filesystem::remove(path);

  auto old_version = solve.value().postflop_policy;
  old_version.minor = 6U;
  old_version.fingerprint = gtosd::fingerprint_hu_preflop_sampled_postflop_policy(old_version);
  require(!gtosd::validate_hu_preflop_sampled_postflop_policy(tree, old_version) &&
              !gtosd::serialize_hu_preflop_sampled_postflop_policy(old_version),
          "street-adaptive-v8 cannot be mislabeled as policy format 1.6");

  auto invalid_options = options_for(
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthStreetAdaptiveV8,
      {16U, 128U, 512U});
  require(!gtosd::solve_hu_preflop_sampled(tree, invalid_options),
          "street-adaptive-v8 solve rejects a capacity below 32");
}

void test_distributional_selective_history_v9_policy(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthSelectiveHistoryV9,
          capacities));
  require(solve.has_value(), "distributional selective-history-v9 solve completes");
  require(solve.value().abstraction_id.find(
              "category_equity_ordered_profile_selective_history_v9") != std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::
                      DistributionalStrengthSelectiveHistoryV9 &&
              solve.value().postflop_policy.minor == 8U &&
              gtosd::validate_hu_preflop_sampled_postflop_policy(
                  tree, solve.value().postflop_policy)
                  .has_value(),
          "selective-history-v9 has a distinct identity and valid policy format 1.8");

  std::array<std::uint64_t, 3> entries_by_street{};
  const auto unset = gtosd::hu_preflop_sampled_postflop_unset_bucket;
  for (const auto &entry : solve.value().postflop_policy.entries) {
    const auto street_index =
        static_cast<std::size_t>(entry.key.street) - static_cast<std::size_t>(gtosd::Street::Flop);
    ++entries_by_street[street_index];
    require(entry.key.preflop_class == 0U,
            "selective-history-v9 does not retain the preflop class");
    for (std::size_t index = 0U; index < entry.key.bucket_history.size(); ++index) {
      const bool expected =
          index <= street_index && (street_index == 0U || index + 1U >= street_index);
      require((entry.key.bucket_history[index] != unset) == expected,
              "selective-history-v9 retains exactly the current and previous street buckets");
    }
  }
  require(std::ranges::all_of(entries_by_street, [](const auto count) { return count > 0U; }),
          "selective-history-v9 exercises Flop, Turn and River keys");

  const auto path =
      std::filesystem::temp_directory_path() / "gtosd_r6_selective_history_v9_policy.bin";
  require(gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy,
                                                          path.string())
              .has_value(),
          "selective-history-v9 policy saves atomically");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() && loaded.value().minor == 8U &&
              loaded.value().representation ==
                  gtosd::HuPreflopPostflopRepresentation::
                      DistributionalStrengthSelectiveHistoryV9 &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint,
          "selective-history-v9 policy survives checksum validation and reload");
  std::filesystem::remove(path);

  auto old_version = solve.value().postflop_policy;
  old_version.minor = 7U;
  old_version.fingerprint = gtosd::fingerprint_hu_preflop_sampled_postflop_policy(old_version);
  require(!gtosd::validate_hu_preflop_sampled_postflop_policy(tree, old_version) &&
              !gtosd::serialize_hu_preflop_sampled_postflop_policy(old_version),
          "selective-history-v9 cannot be mislabeled as policy format 1.7");

  const auto river = std::ranges::find_if(
      solve.value().postflop_policy.entries,
      [](const auto &entry) { return entry.key.street == gtosd::Street::River; });
  require(river != solve.value().postflop_policy.entries.end(),
          "selective-history-v9 policy contains a River entry");
  auto invalid_key = river->key;
  invalid_key.bucket_history[0] = 0U;
  const auto invalid_query = gtosd::query_hu_preflop_sampled_postflop_policy(
      solve.value().postflop_policy, invalid_key, river->action_count);
  require(!invalid_query && invalid_query.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "selective-history-v9 rejects a River key that incorrectly retains the Flop bucket");
}

void test_distributional_category_history_v10_policy(const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  constexpr auto partition_seed = 0x5235'5041'5254'0001ULL;
  const std::array hole{gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value()};
  const std::array board{gtosd::parse_card("Qh").value(), gtosd::parse_card("9c").value(),
                         gtosd::parse_card("7d").value(), gtosd::parse_card("8s").value(),
                         gtosd::parse_card("6c").value()};
  constexpr std::array streets{gtosd::Street::Flop, gtosd::Street::Turn,
                               gtosd::Street::River};
  for (std::size_t index = 0U; index < streets.size(); ++index) {
    const auto bucket = gtosd::compute_hu_preflop_distributional_structured_v7_bucket(
        hole, board, streets[index], 8U, partition_seed, capacities);
    const auto capacity_bits = std::countr_zero(static_cast<std::uint32_t>(capacities[index]));
    require(bucket.has_value() &&
                (bucket.value() >> (capacity_bits - 4U)) ==
                    static_cast<std::uint16_t>(
                        visible_category_for_test(hole, board, streets[index])),
            "category-history-v10 extracts the exact visible category from the v7 bucket");
  }

  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(
          gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthCategoryHistoryV10,
          capacities));
  require(solve.has_value(), "distributional category-history-v10 solve completes");
  require(solve.value().abstraction_id.find(
              "category_equity_ordered_profile_category_history_v10") != std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::
                      DistributionalStrengthCategoryHistoryV10 &&
              solve.value().postflop_policy.minor == 9U &&
              gtosd::validate_hu_preflop_sampled_postflop_policy(
                  tree, solve.value().postflop_policy)
                  .has_value(),
          "category-history-v10 has a distinct identity and valid policy format 1.9");

  std::array<std::uint64_t, 3> entries_by_street{};
  const auto unset = gtosd::hu_preflop_sampled_postflop_unset_bucket;
  for (const auto &entry : solve.value().postflop_policy.entries) {
    const auto street_index =
        static_cast<std::size_t>(entry.key.street) - static_cast<std::size_t>(gtosd::Street::Flop);
    ++entries_by_street[street_index];
    require(entry.key.preflop_class == 0U,
            "category-history-v10 does not retain the preflop class");
    for (std::size_t index = 0U; index < entry.key.bucket_history.size(); ++index) {
      const bool expected =
          index <= street_index && (street_index == 0U || index + 1U >= street_index);
      require((entry.key.bucket_history[index] != unset) == expected,
              "category-history-v10 retains exactly history category and current bucket");
    }
    if (street_index > 0U) {
      require(entry.key.bucket_history[street_index - 1U] <=
                  static_cast<std::uint16_t>(gtosd::HandCategory::StraightFlush),
              "category-history-v10 historical code stays in the exact category domain");
    }
  }
  require(std::ranges::all_of(entries_by_street, [](const auto count) { return count > 0U; }),
          "category-history-v10 exercises Flop, Turn and River keys");

  const auto path =
      std::filesystem::temp_directory_path() / "gtosd_r6_category_history_v10_policy.bin";
  require(gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy,
                                                          path.string())
              .has_value(),
          "category-history-v10 policy saves atomically");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() && loaded.value().minor == 9U &&
              loaded.value().representation ==
                  gtosd::HuPreflopPostflopRepresentation::
                      DistributionalStrengthCategoryHistoryV10 &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint,
          "category-history-v10 policy survives checksum validation and reload");
  std::filesystem::remove(path);

  auto old_version = solve.value().postflop_policy;
  old_version.minor = 8U;
  old_version.fingerprint = gtosd::fingerprint_hu_preflop_sampled_postflop_policy(old_version);
  require(!gtosd::validate_hu_preflop_sampled_postflop_policy(tree, old_version) &&
              !gtosd::serialize_hu_preflop_sampled_postflop_policy(old_version),
          "category-history-v10 cannot be mislabeled as policy format 1.8");

  const auto river = std::ranges::find_if(
      solve.value().postflop_policy.entries,
      [](const auto &entry) { return entry.key.street == gtosd::Street::River; });
  require(river != solve.value().postflop_policy.entries.end(),
          "category-history-v10 policy contains a River entry");
  auto invalid_key = river->key;
  invalid_key.bucket_history[1] = 9U;
  const auto invalid_query = gtosd::query_hu_preflop_sampled_postflop_policy(
      solve.value().postflop_policy, invalid_key, river->action_count);
  require(!invalid_query && invalid_query.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "category-history-v10 rejects an out-of-domain historical category");
}

void test_distributional_adaptive_category_history_v11_policy(
    const gtosd::HuPreflopTree &tree) {
  constexpr std::array<std::uint16_t, 3> capacities{32U, 128U, 512U};
  constexpr auto partition_seed = 0x5235'5041'5254'0001ULL;
  const std::array hole{gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value()};
  const std::array board{gtosd::parse_card("Qh").value(), gtosd::parse_card("9c").value(),
                         gtosd::parse_card("7d").value(), gtosd::parse_card("8s").value(),
                         gtosd::parse_card("6c").value()};
  constexpr std::array streets{gtosd::Street::Flop, gtosd::Street::Turn,
                               gtosd::Street::River};
  for (const auto street : streets) {
    const auto v8 = gtosd::compute_hu_preflop_distributional_street_adaptive_v8_bucket(
        hole, board, street, 8U, partition_seed, capacities);
    const auto v11 =
        gtosd::compute_hu_preflop_distributional_adaptive_category_history_v11_bucket(
            hole, board, street, 8U, partition_seed, capacities);
    require(v8.has_value() && v11.has_value() && v8.value() == v11.value(),
            "adaptive-category-history-v11 preserves the exact v8 current-street mapping");
  }

  const auto solve = gtosd::solve_hu_preflop_sampled(
      tree,
      options_for(gtosd::HuPreflopPostflopRepresentation::
                      DistributionalStrengthAdaptiveCategoryHistoryV11,
                  capacities));
  require(solve.has_value(), "distributional adaptive-category-history-v11 solve completes");
  require(solve.value().abstraction_id.find(
              "street_adaptive_category_equity_profile_category_history_v11") !=
                  std::string::npos &&
              solve.value().postflop_policy.representation ==
                  gtosd::HuPreflopPostflopRepresentation::
                      DistributionalStrengthAdaptiveCategoryHistoryV11 &&
              solve.value().postflop_policy.minor == 10U &&
              gtosd::validate_hu_preflop_sampled_postflop_policy(
                  tree, solve.value().postflop_policy)
                  .has_value(),
          "adaptive-category-history-v11 has a distinct identity and valid policy format 1.10");

  std::array<std::uint64_t, 3> entries_by_street{};
  const auto unset = gtosd::hu_preflop_sampled_postflop_unset_bucket;
  for (const auto &entry : solve.value().postflop_policy.entries) {
    const auto street_index =
        static_cast<std::size_t>(entry.key.street) - static_cast<std::size_t>(gtosd::Street::Flop);
    ++entries_by_street[street_index];
    require(entry.key.preflop_class == 0U,
            "adaptive-category-history-v11 does not retain the preflop class");
    for (std::size_t index = 0U; index < entry.key.bucket_history.size(); ++index) {
      const bool expected =
          index <= street_index && (street_index == 0U || index + 1U >= street_index);
      require((entry.key.bucket_history[index] != unset) == expected,
              "adaptive-category-history-v11 retains one category and the current bucket");
    }
    if (street_index > 0U) {
      require(entry.key.bucket_history[street_index - 1U] <=
                  static_cast<std::uint16_t>(gtosd::HandCategory::StraightFlush),
              "adaptive-category-history-v11 history stays in the exact category domain");
    }
    require(entry.key.bucket_history[street_index] < capacities[street_index],
            "adaptive-category-history-v11 current bucket respects the v8 capacity");
  }
  require(std::ranges::all_of(entries_by_street, [](const auto count) { return count > 0U; }),
          "adaptive-category-history-v11 exercises Flop, Turn and River keys");

  const auto path = std::filesystem::temp_directory_path() /
                    "gtosd_r6_adaptive_category_history_v11_policy.bin";
  require(gtosd::save_hu_preflop_sampled_postflop_policy(solve.value().postflop_policy,
                                                          path.string())
              .has_value(),
          "adaptive-category-history-v11 policy saves atomically");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree, path.string());
  require(loaded.has_value() && loaded.value().minor == 10U &&
              loaded.value().representation ==
                  gtosd::HuPreflopPostflopRepresentation::
                      DistributionalStrengthAdaptiveCategoryHistoryV11 &&
              loaded.value().fingerprint == solve.value().postflop_policy.fingerprint,
          "adaptive-category-history-v11 policy survives checksum validation and reload");
  std::filesystem::remove(path);

  auto old_version = solve.value().postflop_policy;
  old_version.minor = 9U;
  old_version.fingerprint = gtosd::fingerprint_hu_preflop_sampled_postflop_policy(old_version);
  require(!gtosd::validate_hu_preflop_sampled_postflop_policy(tree, old_version) &&
              !gtosd::serialize_hu_preflop_sampled_postflop_policy(old_version),
          "adaptive-category-history-v11 cannot be mislabeled as policy format 1.9");

  const auto river = std::ranges::find_if(
      solve.value().postflop_policy.entries,
      [](const auto &entry) { return entry.key.street == gtosd::Street::River; });
  require(river != solve.value().postflop_policy.entries.end(),
          "adaptive-category-history-v11 policy contains a River entry");
  auto invalid_key = river->key;
  invalid_key.bucket_history[1] = 9U;
  const auto invalid_query = gtosd::query_hu_preflop_sampled_postflop_policy(
      solve.value().postflop_policy, invalid_key, river->action_count);
  require(!invalid_query && invalid_query.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "adaptive-category-history-v11 rejects an out-of-domain historical category");
}

} // namespace

int main() {
  try {
    test_mapping_contract();
    test_distributional_structured_v7_mapping();
    test_distributional_street_adaptive_v8_mapping();
    const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
    require(tree.has_value(), "CO40 tree builds for R5 tests");
    test_profile_v6_uses_lower_distributional_features();
    test_persisted_mapping_and_coverage(tree.value());
    test_capacity_comparison(tree.value());
    test_distributional_perfect_recall(tree.value());
    test_distributional_bucket_history(tree.value());
    test_distributional_profile_v6(tree.value());
    test_distributional_structured_v7_policy(tree.value());
    test_distributional_street_adaptive_v8_policy(tree.value());
    test_distributional_selective_history_v9_policy(tree.value());
    test_distributional_category_history_v10_policy(tree.value());
    test_distributional_adaptive_category_history_v11_policy(tree.value());
    std::cout << "R5_HU_PREFLOP_ABSTRACTION_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "R5_HU_PREFLOP_ABSTRACTION_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "R5_HU_PREFLOP_ABSTRACTION_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
