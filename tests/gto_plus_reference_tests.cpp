#include "gtosd/memory/memory.hpp"
#include "gtosd/postflop/postflop_solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::size_t assertions = 0;

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

gtosd::PostflopTreeConfig make_reference_config() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  config.flop = {gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value(),
                 gtosd::parse_card("Qh").value()};
  config.initial_pot = gtosd::Money::from_antes(40).value();
  config.effective_stack = gtosd::Money::from_antes(100).value();
  config.rake.enabled = true;
  config.rake.percentage = gtosd::RangeWeight::from_basis_points(0).value();
  config.rake.cap = gtosd::Money{};
  const auto seventy_five = gtosd::PotPercentage::from_basis_points(7'500).value();
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      player[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = {
          seventy_five};
      player[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes = {
          seventy_five};
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].aggressive_sizes.clear();
      for (auto &scenario : player) {
        scenario.raise_depth = 0U;
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
      }
    }
  }
  return config;
}

gtosd::PostflopRanges make_reference_ranges() {
  gtosd::PostflopRanges ranges;
  const auto zero = gtosd::RangeWeight::from_basis_points(0).value();
  const auto full = gtosd::RangeWeight::from_basis_points(10'000).value();
  for (auto &range : ranges.players) {
    range.fill(zero);
  }
  constexpr std::array<std::string_view, 9> selected_classes{"AA",  "KK",  "QQ",  "AKs", "AQs",
                                                             "KQs", "AKo", "AQo", "KQo"};
  const auto combos = gtosd::all_combos();
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    const auto name = gtosd::class_name(gtosd::hand_class(combos[combo]));
    if (std::ranges::find(selected_classes, name) != selected_classes.end()) {
      ranges.players[0][combo] = full;
      ranges.players[1][combo] = full;
    }
  }
  return ranges;
}

void test_exact_reference_build() {
  constexpr std::uint64_t expected_nodes = 52'644U;
  constexpr std::uint64_t expected_physical_infosets = 683'136U;
  constexpr std::uint64_t expected_physical_actions = 1'366'272U;
  constexpr std::uint64_t expected_canonical_infosets = 125'352U;
  constexpr std::uint64_t expected_canonical_actions = 250'704U;
  constexpr std::uint64_t expected_canonical_public_nodes = 14'673U;
  constexpr std::uint64_t expected_action_buffer_bytes =
      expected_canonical_actions * 2U * sizeof(double);

  const auto config = make_reference_config();
  const auto ranges = make_reference_ranges();
  const auto combos = gtosd::all_combos();
  const auto board_mask = config.flop[0].mask() | config.flop[1].mask() | config.flop[2].mask();
  std::size_t compatible_source_combos = 0U;
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    const auto combo_mask = combos[combo].first.mask() | combos[combo].second.mask();
    if (ranges.players[0][combo].basis_points() != 0U && (combo_mask & board_mask) == 0U) {
      ++compatible_source_combos;
    }
  }
  require(compatible_source_combos == 36U,
          "reference range has exactly 36 physical combos after AhKhQh blockers");

  const auto estimate =
      gtosd::analyze_postflop_config(config, ranges, gtosd::MemoryPrototype::LazyInRam);
  require(estimate.has_value(), "range-aware reference estimate succeeds");
  require(estimate.value().public_tree.node_count == expected_nodes &&
              estimate.value().information_sets == expected_physical_infosets &&
              estimate.value().actions == expected_physical_actions,
          "range-aware estimate reports the exact physical reference counts");

  gtosd::PostflopSolveOptions options;
  options.iterations = 1U;
  options.certification_interval = 1U;
  const auto started = std::chrono::steady_clock::now();
  const auto solved = gtosd::solve_postflop_exact(config, ranges, options);
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
  require(solved.has_value(), "range-aware exact reference solve succeeds");
  require(solved.value().public_tree.node_count == expected_nodes &&
              solved.value().information_sets == expected_canonical_infosets &&
              solved.value().actions == expected_canonical_actions &&
              solved.value().canonical_public_nodes == expected_canonical_public_nodes,
          "materialized solver layout matches the lossless canonical golden counts");
  require(solved.value().checkpoint.cumulative_regret.size() == expected_canonical_actions &&
              solved.value().checkpoint.cumulative_strategy.size() == expected_canonical_actions,
          "checkpoint stores only lossless canonical action entries");
  require(expected_action_buffer_bytes == 4'011'264U,
          "two canonical float64 CFR buffers have the exact reference byte count");

  std::cout << "GTO_PLUS_REFERENCE_TEST=PASS"
            << " assertions=" << assertions << " nodes=" << expected_nodes
            << " physical_infosets=" << expected_physical_infosets
            << " physical_actions=" << expected_physical_actions
            << " canonical_infosets=" << expected_canonical_infosets
            << " canonical_actions=" << expected_canonical_actions
            << " canonical_public_nodes=" << solved.value().canonical_public_nodes
            << " traversed_nodes=" << solved.value().traversed_nodes
            << " action_buffer_bytes=" << expected_action_buffer_bytes
            << " estimated_peak_bytes=" << estimate.value().memory.peak_resident_bytes
            << " public_tree_bytes=" << estimate.value().memory.public_tree_bytes
            << " layout_index_bytes=" << estimate.value().memory.infoset_index_bytes
            << " traversal_scratch_bytes=" << estimate.value().memory.reach_bytes
            << " measured_peak_rss_bytes=" << gtosd::process_peak_rss_bytes()
            << " elapsed_seconds=" << elapsed.count() << '\n';
}

void test_lossless_isomorphism_matches_physical_cfr() {
  const auto config = make_reference_config();
  const auto ranges = make_reference_ranges();
  gtosd::PostflopSolveOptions physical_options;
  physical_options.iterations = 2U;
  physical_options.certification_interval = 2U;
  physical_options.enable_lossless_isomorphism = false;
  physical_options.enable_canonical_public_dag = false;
  const auto physical = gtosd::solve_postflop_exact(config, ranges, physical_options);
  require(physical.has_value(), "physical parity solve succeeds");

  auto canonical_physical_options = physical_options;
  canonical_physical_options.enable_lossless_isomorphism = true;
  const auto canonical_physical =
      gtosd::solve_postflop_exact(config, ranges, canonical_physical_options);
  require(canonical_physical.has_value(), "canonical physical-traversal parity solve succeeds");

  auto dag_options = canonical_physical_options;
  dag_options.enable_canonical_public_dag = true;
  const auto dag = gtosd::solve_postflop_exact(config, ranges, dag_options);
  require(dag.has_value(), "canonical DAG parity solve succeeds");
  require(dag.value().information_sets < physical.value().information_sets &&
              dag.value().actions < physical.value().actions &&
              dag.value().canonical_public_nodes < dag.value().public_tree.node_count,
          "lossless isomorphism reduces the materialized CFR state");
  require(physical.value().convergence.size() == 1U &&
              canonical_physical.value().convergence.size() == 1U &&
              dag.value().convergence.size() == 1U,
          "all parity solves produce one comparable certification");
  const auto &physical_certification = physical.value().convergence.front();
  const auto &canonical_certification = dag.value().convergence.front();
  constexpr double tolerance = 1e-11;
  for (std::size_t player = 0; player < 2U; ++player) {
    require(std::abs(physical_certification.profile_value_antes[player] -
                     canonical_certification.profile_value_antes[player]) < tolerance,
            "canonical profile EV matches the physical traversal");
    require(std::abs(physical_certification.best_response_value_antes[player] -
                     canonical_certification.best_response_value_antes[player]) < tolerance,
            "canonical best-response EV matches the physical traversal");
  }
  require(std::abs(physical_certification.nash_conv_antes -
                   canonical_certification.nash_conv_antes) < tolerance,
          "canonical NashConv matches the physical traversal");
  require(physical.value().checkpoint.game_fingerprint != dag.value().checkpoint.game_fingerprint,
          "physical and canonical checkpoint layouts cannot be mixed");
  require(canonical_physical.value().checkpoint.game_fingerprint ==
                  dag.value().checkpoint.game_fingerprint &&
              canonical_physical.value().checkpoint.cumulative_regret.size() ==
                  dag.value().checkpoint.cumulative_regret.size(),
          "physical and DAG traversals share the same canonical checkpoint layout");
  double maximum_regret_difference = 0.0;
  double maximum_strategy_difference = 0.0;
  std::size_t maximum_regret_action = 0U;
  for (std::size_t action = 0; action < dag.value().checkpoint.cumulative_regret.size(); ++action) {
    const auto regret_difference =
        std::abs(canonical_physical.value().checkpoint.cumulative_regret[action] -
                 dag.value().checkpoint.cumulative_regret[action]);
    if (regret_difference > maximum_regret_difference) {
      maximum_regret_difference = regret_difference;
      maximum_regret_action = action;
    }
    maximum_strategy_difference =
        std::max(maximum_strategy_difference,
                 std::abs(canonical_physical.value().checkpoint.cumulative_strategy[action] -
                          dag.value().checkpoint.cumulative_strategy[action]));
  }
  if (maximum_regret_difference >= tolerance || maximum_strategy_difference >= tolerance) {
    std::cerr << "DAG_PARITY_DIAGNOSTIC max_regret_difference=" << maximum_regret_difference
              << " max_regret_action=" << maximum_regret_action << " physical_regret="
              << canonical_physical.value().checkpoint.cumulative_regret[maximum_regret_action]
              << " dag_regret=" << dag.value().checkpoint.cumulative_regret[maximum_regret_action]
              << " max_strategy_difference=" << maximum_strategy_difference << '\n';
  }
  require(maximum_regret_difference < tolerance,
          "DAG regret matches the canonical physical traversal");
  require(maximum_strategy_difference < tolerance,
          "DAG average-strategy sum matches the canonical physical traversal");
  std::cout << "CANONICAL_DAG_PARITY_TEST=PASS"
            << " physical_public_nodes=" << dag.value().public_tree.node_count
            << " canonical_public_nodes=" << dag.value().canonical_public_nodes
            << " maximum_regret_difference=" << maximum_regret_difference
            << " maximum_strategy_difference=" << maximum_strategy_difference << '\n';
}

} // namespace

int main() {
  try {
    test_exact_reference_build();
    test_lossless_isomorphism_matches_physical_cfr();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "GTO_PLUS_REFERENCE_TEST=FAIL assertions=" << assertions
              << " error=" << error.what() << '\n';
    return 1;
  }
}
