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
  const auto budgeted =
      gtosd::estimate_canonical_chance_layout(config, ranges, budgeted_options);
  require(budgeted.has_value(), "layout accepts an explicit experiment budget");
  for (const auto &memory : budgeted.value().memory_estimates) {
    require(memory.meets_requested_budget.has_value() &&
                !memory.meets_requested_budget.value(),
            "explicit one-byte budget is evaluated without becoming a default gate");
  }

  gtosd::CanonicalLayoutOptions invalid_options;
  invalid_options.requested_budget =
      gtosd::CanonicalLayoutBudget{0U, gtosd::CanonicalLayoutBudgetSource::UserConfigured};
  require(!gtosd::estimate_canonical_chance_layout(config, ranges, invalid_options).has_value(),
          "zero-byte explicit budget is rejected");
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
