#include "gtosd/memory/memory.hpp"
#include "gtosd/postflop/canonical_layout.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::size_t assertions = 0U;

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

gtosd::PostflopTreeConfig make_config() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  config.flop = {gtosd::parse_card("Ts").value(), gtosd::parse_card("Tc").value(),
                 gtosd::parse_card("9d").value()};
  config.initial_pot = gtosd::Money::from_antes(16).value();
  config.effective_stack = gtosd::Money::from_antes(80).value();
  return config;
}

gtosd::PostflopRanges make_asymmetric_suit_invariant_ranges() {
  gtosd::PostflopRanges ranges;
  const auto zero = gtosd::RangeWeight::from_basis_points(0U).value();
  const auto full = gtosd::RangeWeight::from_basis_points(10'000U).value();
  ranges.players[0].fill(full);
  ranges.players[1].fill(zero);
  const auto combos = gtosd::all_combos();
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    const auto name = gtosd::class_name(gtosd::hand_class(combos[combo]));
    if (name.size() == 2U || name.ends_with('s')) {
      ranges.players[1][combo] = full;
    }
  }
  return ranges;
}

void test_asymmetric_range_canonical_layout() {
  const auto config = make_config();
  const auto ranges = make_asymmetric_suit_invariant_ranges();
  require(ranges.players[0] != ranges.players[1], "fixture ranges are asymmetric");
  const auto report = gtosd::estimate_canonical_chance_layout(config, ranges);
  require(report.has_value(), "layout-only compiler succeeds");
  require(report.value().preserving_suit_automorphisms == 2U,
          "TsTc9d retains exactly the clubs-spades swap for both ranges");
  std::cout << "CANONICAL_BOARD_PATHS flop=" << report.value().streets[0].canonical_board_paths
            << " turn=" << report.value().streets[1].canonical_board_paths
            << " river=" << report.value().streets[2].canonical_board_paths << '\n';
  require(report.value().streets[0].physical_board_paths == 1U &&
              report.value().streets[0].canonical_board_paths == 1U &&
              report.value().streets[1].physical_board_paths == 33U &&
              report.value().streets[1].canonical_board_paths == 25U &&
              report.value().streets[2].physical_board_paths == 1'056U &&
              report.value().streets[2].canonical_board_paths == 664U,
          "orbit representative counts preserve ordered turn-river paths");
  require(report.value().canonical_public_nodes < report.value().physical_public_tree.node_count,
          "canonical chance tree has fewer public nodes than the physical tree");
  require(report.value().information_sets > 0U && report.value().action_entries > 0U,
          "layout reports non-empty exact state");
  require(report.value().memory_estimates.size() == 8U,
          "layout reports both codecs for 1, 2, 4 and 8 workers");
  for (const auto &memory : report.value().memory_estimates) {
    require(memory.solver_state_bytes ==
                report.value().action_entries * memory.state_bytes_per_action,
            "state byte accounting is exact");
    require(!memory.meets_requested_budget.has_value(),
            "default layout estimate has no implicit memory budget");
  }

  gtosd::CanonicalLayoutOptions budgeted_options;
  budgeted_options.requested_budget =
      gtosd::CanonicalLayoutBudget{1U, gtosd::CanonicalLayoutBudgetSource::Experiment};
  const auto budgeted = gtosd::estimate_canonical_chance_layout(config, ranges, budgeted_options);
  require(budgeted.has_value(), "layout accepts an explicit experiment budget");
  for (const auto &memory : budgeted.value().memory_estimates) {
    require(memory.meets_requested_budget.has_value() && !memory.meets_requested_budget.value(),
            "explicit one-byte budget is evaluated without becoming a default gate");
  }

  gtosd::CanonicalLayoutOptions abstraction_options;
  abstraction_options.card_abstraction_buckets = {1U, 8U, 630U};
  abstraction_options.requested_budget =
      gtosd::CanonicalLayoutBudget{1U, gtosd::CanonicalLayoutBudgetSource::Experiment};
  abstraction_options.requested_feature_cache_disk_budget =
      gtosd::CanonicalLayoutBudget{1U, gtosd::CanonicalLayoutBudgetSource::Experiment};
  const auto abstraction =
      gtosd::estimate_canonical_chance_layout(config, ranges, abstraction_options);
  require(abstraction.has_value() && abstraction.value().card_abstraction_preflight.has_value(),
          "card-abstraction preflight succeeds without allocating solver state");
  const auto &preflight = *abstraction.value().card_abstraction_preflight;
  require(preflight.feature_cache_worker_count == 8U && preflight.solver_thread_count == 8U &&
              preflight.partition_count > 0U && preflight.observation_count > 0U &&
              preflight.maximum_partition_observations > 0U,
          "preflight uses the eight-thread production contract and counts feature partitions");
  require(preflight.feature_dimensions == 4U && preflight.feature_cache_logical_bytes > 0U &&
              preflight.feature_cache_serialized_bytes_upper_bound > 0U &&
              preflight.feature_cache_serialized_bytes_upper_bound >=
                  preflight.observation_count * 139U &&
              preflight.feature_cache_atomic_write_bytes_upper_bound ==
                  preflight.feature_cache_serialized_bytes_upper_bound * 2U &&
              preflight.feature_cache_format_limit_ok,
          "preflight accounts the native feature-cache contract and atomic write");
  require(preflight.feature_cache_atomic_write_meets_requested_budget.has_value() &&
              !*preflight.feature_cache_atomic_write_meets_requested_budget,
          "explicit disk budget is evaluated separately");
  require(preflight.candidates.size() == 3U,
          "preflight preserves every requested bucket candidate");
  const auto &one_bucket = preflight.candidates[0];
  const auto &eight_buckets = preflight.candidates[1];
  const auto &identity_upper_bound = preflight.candidates[2];
  require(one_bucket.abstract_information_sets_upper_bound <
                  eight_buckets.abstract_information_sets_upper_bound &&
              eight_buckets.abstract_information_sets_upper_bound <
                  identity_upper_bound.abstract_information_sets_upper_bound,
          "candidate state grows with bucket granularity");
  require(identity_upper_bound.abstract_information_sets_upper_bound ==
                  abstraction.value().information_sets &&
              identity_upper_bound.abstract_action_entries_upper_bound ==
                  abstraction.value().action_entries,
          "K=630 reaches the exact combo-state upper bound");
  for (const auto &candidate : preflight.candidates) {
    require(candidate.solver_state_bytes_upper_bound ==
                    candidate.abstract_action_entries_upper_bound * 2U * sizeof(double) &&
                candidate.estimated_in_ram_peak_bytes >= candidate.solver_state_bytes_upper_bound &&
                candidate.estimated_out_of_core_peak_bytes ==
                    candidate.estimated_in_ram_peak_bytes &&
                candidate.estimated_disk_bytes_with_cache >=
                    candidate.out_of_core_backing_store_bytes +
                        preflight.feature_cache_atomic_write_bytes_upper_bound &&
                candidate.in_ram_meets_requested_budget.has_value() &&
                !*candidate.in_ram_meets_requested_budget &&
                candidate.out_of_core_meets_requested_budgets.has_value() &&
                !*candidate.out_of_core_meets_requested_budgets,
            "candidate uses the conservative Float64 CFR+ RAM and disk gate");
  }

  gtosd::CanonicalLayoutOptions invalid_options;
  invalid_options.requested_budget =
      gtosd::CanonicalLayoutBudget{0U, gtosd::CanonicalLayoutBudgetSource::UserConfigured};
  require(!gtosd::estimate_canonical_chance_layout(config, ranges, invalid_options).has_value(),
          "zero-byte explicit budget is rejected");
  invalid_options = {};
  invalid_options.card_abstraction_buckets = {0U};
  require(!gtosd::estimate_canonical_chance_layout(config, ranges, invalid_options).has_value(),
          "zero bucket candidate is rejected before enumeration");
}

} // namespace

int main() {
  try {
    test_asymmetric_range_canonical_layout();
    std::cout << "CANONICAL_LAYOUT_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CANONICAL_LAYOUT_TESTS=FAIL assertions=" << assertions
              << " error=" << error.what() << '\n';
    return 1;
  }
}
