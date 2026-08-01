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
  const auto half_pot = gtosd::PotPercentage::from_basis_points(5'000).value();
  const auto go_all_in_threshold = gtosd::PotPercentage::from_basis_points(15'000).value();
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      player[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = {half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes = {
          half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].aggressive_sizes = {
          half_pot};
      for (auto &scenario : player) {
        scenario.raise_depth = 0U;
        scenario.all_in_mode = gtosd::AllInMode::Go;
        scenario.all_in_threshold = go_all_in_threshold;
      }
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].raise_depth = 1U;
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
  constexpr std::uint64_t expected_nodes = 112'848U;
  constexpr std::uint64_t expected_physical_infosets = 1'366'272U;
  constexpr std::uint64_t expected_physical_actions = 2'871'072U;
  constexpr std::uint64_t expected_canonical_infosets = 250'704U;
  constexpr std::uint64_t expected_canonical_actions = 526'872U;
  constexpr std::uint64_t expected_canonical_public_nodes = 31'461U;
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
          "range-aware estimate reports the exact corrected physical counts (actual nodes=" +
              std::to_string(estimate.value().public_tree.node_count) +
              ", infosets=" + std::to_string(estimate.value().information_sets) +
              ", actions=" + std::to_string(estimate.value().actions) + ")");
  const auto materialized = gtosd::estimate_postflop_layout(config, ranges);
  require(materialized.has_value() &&
              materialized.value().canonical_public_nodes == expected_canonical_public_nodes &&
              materialized.value().information_sets == expected_canonical_infosets &&
              materialized.value().actions == expected_canonical_actions &&
              materialized.value().regret_bytes + materialized.value().strategy_bytes ==
                  expected_action_buffer_bytes,
          "materialized preflight reports the canonical CFR state (actual public_nodes=" +
              std::to_string(materialized.value().canonical_public_nodes) +
              ", infosets=" + std::to_string(materialized.value().information_sets) +
              ", actions=" + std::to_string(materialized.value().actions) + ")");

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
  require(expected_action_buffer_bytes == 8'429'952U,
          "two canonical float64 CFR buffers have the exact corrected reference byte count");
  auto float32_options = options;
  float32_options.state_precision = gtosd::PostflopStatePrecision::Float32;
  const auto float32_solved = gtosd::solve_postflop_exact(config, ranges, float32_options);
  require(float32_solved.has_value() &&
              float32_solved.value().checkpoint.cumulative_regret.empty() &&
              float32_solved.value().checkpoint.cumulative_strategy.empty() &&
              float32_solved.value().checkpoint.cumulative_regret_float32.size() ==
                  expected_canonical_actions &&
              float32_solved.value().checkpoint.cumulative_strategy_float32.size() ==
                  expected_canonical_actions,
          "float32 performance state stores both canonical buffers in 4.214976 MB");
  const auto float32_certified =
      gtosd::certify_postflop_checkpoint(config, ranges, float32_solved.value().checkpoint);
  const auto float32_analysis =
      gtosd::analyze_postflop_node(config, ranges, float32_solved.value().checkpoint, 0U);
  require(float32_certified.has_value() && float32_analysis.has_value() &&
              float32_analysis.value().combos.size() == 36U,
          "float32 performance checkpoint remains exactly certifiable and browsable");
  require(std::abs(float32_certified.value().normalized_nash_conv -
                   solved.value().convergence.back().normalized_nash_conv) < 1e-6,
          "float32 state stays within the declared one-iteration differential tolerance");
  auto parallel_float32_options = float32_options;
  parallel_float32_options.parallel_action_depth = 5U;
  const auto parallel_float32 =
      gtosd::solve_postflop_exact(config, ranges, parallel_float32_options);
  require(parallel_float32.has_value(), "parallel float32 performance solve succeeds");
  double maximum_parallel_regret_difference = 0.0;
  double maximum_parallel_strategy_difference = 0.0;
  for (std::size_t action = 0; action < expected_canonical_actions; ++action) {
    maximum_parallel_regret_difference = std::max(
        maximum_parallel_regret_difference,
        std::abs(static_cast<double>(
                     parallel_float32.value().checkpoint.cumulative_regret_float32[action]) -
                 static_cast<double>(
                     float32_solved.value().checkpoint.cumulative_regret_float32[action])));
    maximum_parallel_strategy_difference = std::max(
        maximum_parallel_strategy_difference,
        std::abs(static_cast<double>(
                     parallel_float32.value().checkpoint.cumulative_strategy_float32[action]) -
                 static_cast<double>(
                     float32_solved.value().checkpoint.cumulative_strategy_float32[action])));
  }
  require(maximum_parallel_regret_difference == 0.0 && maximum_parallel_strategy_difference == 0.0,
          "parallel action subtrees reproduce the serial float32 state exactly");
  const auto gto_plus_deviation =
      gtosd::normalized_max_deviation_gain(solved.value().convergence.back(), config.initial_pot);
  const auto &certification = solved.value().convergence.back();
  const double expected_gto_plus_deviation =
      std::max(certification.best_response_value_antes[0] - certification.profile_value_antes[0],
               certification.best_response_value_antes[1] - certification.profile_value_antes[1]) /
      40.0;
  require(gto_plus_deviation.has_value() &&
              std::abs(gto_plus_deviation.value() - expected_gto_plus_deviation) < 1e-15,
          "GTO+ dEV is the maximum unilateral gain divided by the initial pot, not NashConv");
  const auto root_analysis =
      gtosd::analyze_postflop_node(config, ranges, solved.value().checkpoint, 0U);
  require(root_analysis.has_value() && root_analysis.value().combos.size() == 36U &&
              root_analysis.value().actions.size() == 2U,
          "root analysis exposes all 36 reached combos and both actions");
  require(std::abs(root_analysis.value().profile_value_antes[0] -
                   solved.value().convergence.back().profile_value_antes[0]) < 1e-12 &&
              std::abs(root_analysis.value().gto_plus_ev_antes[0] -
                       (root_analysis.value().profile_value_antes[0] + 20.0)) < 1e-12,
          "root node analysis exposes conditional net EV and the GTO+ display convention");
  const auto public_tree = gtosd::build_public_tree(config);
  require(public_tree.has_value(), "corrected reference public tree builds");
  const auto &root = public_tree.value().nodes[public_tree.value().root];
  const auto bet_edge = std::ranges::find_if(
      root.edges, [](const auto &edge) { return edge.action.type == gtosd::ActionType::Bet; });
  require(bet_edge != root.edges.end(), "corrected reference root exposes bet 20");
  const auto &facing = public_tree.value().nodes[bet_edge->child];
  const auto raise = std::ranges::find_if(facing.edges, [](const auto &edge) {
    return edge.action.type == gtosd::ActionType::Raise &&
           edge.action.amount == gtosd::Money::from_antes(60).value();
  });
  const auto all_in = std::ranges::find_if(
      facing.edges, [](const auto &edge) { return edge.action.type == gtosd::ActionType::AllIn; });
  require(
      facing.edges.size() == 3U && raise != facing.edges.end() && all_in == facing.edges.end(),
      "BTN facing bet 20 has fold, call and half-pot raise 60, matching the observed GTO+ tree");
  const auto facing_analysis =
      gtosd::analyze_postflop_node(config, ranges, solved.value().checkpoint, bet_edge->child);
  require(facing_analysis.has_value() && facing_analysis.value().player_to_act == 1U &&
              std::isfinite(facing_analysis.value().gto_plus_ev_antes[1]) &&
              std::abs(facing_analysis.value().gto_plus_ev_antes[1] -
                       (facing_analysis.value().profile_value_antes[1] + 20.0)) < 1e-12,
          "conditional BTN EV after CO bet uses the GTO+ initial-pot display convention");
  double action_sum = 0.0;
  for (const double frequency : root_analysis.value().action_frequencies) {
    action_sum += frequency;
  }
  require(std::abs(action_sum - 1.0) < 1e-12 &&
              std::ranges::all_of(root_analysis.value().combos,
                                  [](const auto &combo) {
                                    return combo.reach_weight > 0.0 && combo.equity >= 0.0 &&
                                           combo.equity <= 1.0 &&
                                           combo.action_probabilities.size() == 2U;
                                  }),
          "root analysis returns normalized frequencies, exact equities and reached weights");

  std::cout << "GTO_PLUS_REFERENCE_TEST=PASS"
            << " assertions=" << assertions << " nodes=" << expected_nodes
            << " physical_infosets=" << estimate.value().information_sets
            << " physical_actions=" << estimate.value().actions
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
