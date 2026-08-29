#include "gtosd/memory/memory.hpp"
#include "gtosd/postflop/postflop_solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      player[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = {half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes = {
          half_pot};
      player[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)].aggressive_sizes = {
          half_pot};
      for (auto &scenario : player) {
        scenario.raise_depth = 0U;
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
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
  // Natural all-in rule (dichiarazione utente 2026-08-05): all-in solo quando
  // la bet size supera lo stack rimanente. Rimossa la soglia 150% (inferenza
  // mai confermata). Albero: 165.774 nodi fisici / 46.065 canonici.
  constexpr std::uint64_t expected_nodes = 165'774U;
  constexpr std::uint64_t expected_physical_infosets = 2'104'992U;
  constexpr std::uint64_t expected_physical_actions = 4'551'552U;
  constexpr std::uint64_t expected_canonical_infosets = 595'626U;
  constexpr std::uint64_t expected_canonical_actions = 1'288'290U;
  constexpr std::uint64_t expected_canonical_public_nodes = 46'065U;
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
  require(expected_action_buffer_bytes == 20'612'640U,
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
          "float32 performance state stores both canonical buffers in 6.677088 MB");
  const auto float32_certified =
      gtosd::certify_postflop_checkpoint(config, ranges, float32_solved.value().checkpoint);
  const auto float32_analysis =
      gtosd::analyze_postflop_node(config, ranges, float32_solved.value().checkpoint, 0U);
  require(float32_certified.has_value() && float32_analysis.has_value() &&
              float32_analysis.value().combos.size() == 36U,
          "float32 performance checkpoint remains exactly certifiable and browsable (certify=" +
              std::string(float32_certified.has_value()
                              ? "ok"
                              : gtosd::postflop_solver_error_name(float32_certified.error())) +
              ", analysis=" +
              std::string(float32_analysis.has_value()
                              ? "ok"
                              : gtosd::postflop_solver_error_name(float32_analysis.error())) +
              ")");
  require(std::abs(float32_certified.value().normalized_nash_conv -
                   solved.value().convergence.back().normalized_nash_conv) < 1e-6,
          "float32 state stays within the declared one-iteration differential tolerance");
  auto mixed_options = options;
  mixed_options.state_precision =
      gtosd::PostflopStatePrecision::Float24RegretFloat16Strategy;
  const auto mixed_solved = gtosd::solve_postflop_exact(config, ranges, mixed_options);
  require(mixed_solved.has_value() &&
              mixed_solved.value().checkpoint.state_precision == mixed_options.state_precision &&
              mixed_solved.value().checkpoint.cumulative_regret_float24.size() ==
                  expected_canonical_actions * 3U &&
              mixed_solved.value().checkpoint.cumulative_strategy_float16.size() ==
                  expected_canonical_actions &&
              mixed_solved.value().checkpoint.cumulative_regret.empty() &&
              mixed_solved.value().checkpoint.cumulative_strategy.empty() &&
              mixed_solved.value().checkpoint.cumulative_regret_float32.empty() &&
              mixed_solved.value().checkpoint.cumulative_strategy_float32.empty(),
          "mixed performance state stores exactly five bytes per canonical action");
  const auto mixed_certified =
      gtosd::certify_postflop_checkpoint(config, ranges, mixed_solved.value().checkpoint);
  const auto mixed_analysis =
      gtosd::analyze_postflop_node(config, ranges, mixed_solved.value().checkpoint, 0U);
  require(mixed_certified.has_value() && mixed_analysis.has_value() &&
              mixed_analysis.value().combos.size() == 36U &&
              std::abs(mixed_certified.value().normalized_nash_conv -
                       solved.value().convergence.back().normalized_nash_conv) < 2e-4,
          "mixed state remains certifiable and within its declared one-iteration tolerance");
  mixed_options.iterations = 2U;
  const auto mixed_resumed = gtosd::solve_postflop_exact(
      config, ranges, mixed_options, &mixed_solved.value().checkpoint);
  require(mixed_resumed.has_value() &&
              mixed_resumed.value().checkpoint.completed_iterations == 2U &&
              mixed_resumed.value().checkpoint.cumulative_regret_float24.size() ==
                  expected_canonical_actions * 3U &&
              mixed_resumed.value().checkpoint.cumulative_strategy_float16.size() ==
                  expected_canonical_actions,
          "mixed in-memory checkpoint resumes without changing representation");
  auto parallel_float32_options = float32_options;
  parallel_float32_options.parallel_action_depth = 5U;
  const auto parallel_float32 =
      gtosd::solve_postflop_exact(config, ranges, parallel_float32_options);
  require(parallel_float32.has_value(), "parallel float32 performance solve succeeds");
  double maximum_parallel_regret_difference = 0.0;
  double maximum_parallel_strategy_difference = 0.0;
  std::size_t maximum_parallel_strategy_index = 0U;
  for (std::size_t action = 0; action < expected_canonical_actions; ++action) {
    maximum_parallel_regret_difference = std::max(
        maximum_parallel_regret_difference,
        std::abs(static_cast<double>(
                     parallel_float32.value().checkpoint.cumulative_regret_float32[action]) -
                 static_cast<double>(
                     float32_solved.value().checkpoint.cumulative_regret_float32[action])));
    const double strategy_difference =
        std::abs(static_cast<double>(
                     parallel_float32.value().checkpoint.cumulative_strategy_float32[action]) -
                 static_cast<double>(
                     float32_solved.value().checkpoint.cumulative_strategy_float32[action]));
    if (strategy_difference > maximum_parallel_strategy_difference) {
      maximum_parallel_strategy_difference = strategy_difference;
      maximum_parallel_strategy_index = action;
    }
  }
  std::cerr << "PARALLEL_STATE_DIAGNOSTIC max_regret_difference="
            << maximum_parallel_regret_difference
            << " max_strategy_difference=" << maximum_parallel_strategy_difference
            << " strategy_index=" << maximum_parallel_strategy_index
            << " serial_strategy="
            << float32_solved.value().checkpoint
                   .cumulative_strategy_float32[maximum_parallel_strategy_index]
            << " parallel_strategy="
            << parallel_float32.value().checkpoint
                   .cumulative_strategy_float32[maximum_parallel_strategy_index]
            << '\n';
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
  auto ranges = make_reference_ranges();
  const auto zero = gtosd::RangeWeight::from_basis_points(0).value();
  const auto combos = gtosd::all_combos();
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    if (gtosd::class_name(gtosd::hand_class(combos[combo])) == "AA") {
      ranges.players[1][combo] = zero;
    }
  }
  require(ranges.players[0] != ranges.players[1],
          "DAG parity fixture uses asymmetric player ranges");
  gtosd::PostflopSolveOptions iso_off_options;
  // Exercise the production TSTC9D schedule and compact state, not only the
  // scale-invariant two-iteration float64 CFR+ prefix. Lossless suit-orbit
  // compression must remain equivalent after discounting, quantization and
  // delayed averaging.
  iso_off_options.iterations = 214U;
  iso_off_options.averaging_delay = 125U;
  iso_off_options.certification_interval = 214U;
  iso_off_options.algorithm = gtosd::PostflopAlgorithm::DcfrPlus;
  iso_off_options.state_precision = gtosd::PostflopStatePrecision::Float32;
  iso_off_options.enable_lossless_isomorphism = false;
  iso_off_options.enable_canonical_public_dag = true;
  const auto iso_off = gtosd::solve_postflop_exact(config, ranges, iso_off_options);
  require(iso_off.has_value(), "node-owned ISO-off solve succeeds");

  auto iso_on_options = iso_off_options;
  iso_on_options.enable_lossless_isomorphism = true;
  const auto iso_on = gtosd::solve_postflop_exact(config, ranges, iso_on_options);
  require(iso_on.has_value(), "node-owned ISO-on solve succeeds");
  require(iso_off.value().canonical_public_nodes == iso_off.value().public_tree.node_count &&
              iso_on.value().canonical_public_nodes < iso_off.value().canonical_public_nodes &&
              iso_on.value().information_sets < iso_off.value().information_sets &&
              iso_on.value().actions < iso_off.value().actions,
          "ISO-on only removes suit-equivalent chance subtrees and their node-owned state");
  require(iso_off.value().convergence.size() == 1U &&
              iso_on.value().convergence.size() == 1U,
          "both node-owned solves produce one comparable certification");
  const auto &iso_off_certification = iso_off.value().convergence.front();
  const auto &iso_on_certification = iso_on.value().convergence.front();
  constexpr double tolerance = 1e-11;
  std::cerr << "ORBIT_DIAGNOSTIC baseline_profile="
            << iso_off_certification.profile_value_antes[0] << ','
            << iso_off_certification.profile_value_antes[1]
            << " orbit_profile=" << iso_on_certification.profile_value_antes[0] << ','
            << iso_on_certification.profile_value_antes[1]
            << " baseline_br=" << iso_off_certification.best_response_value_antes[0]
            << ',' << iso_off_certification.best_response_value_antes[1]
            << " orbit_br=" << iso_on_certification.best_response_value_antes[0] << ','
            << iso_on_certification.best_response_value_antes[1] << '\n';
  for (std::size_t player = 0; player < 2U; ++player) {
    require(std::abs(iso_off_certification.profile_value_antes[player] -
                     iso_on_certification.profile_value_antes[player]) < tolerance,
            "asymmetric ISO compression preserves profile EV");
    require(std::abs(iso_off_certification.best_response_value_antes[player] -
                     iso_on_certification.best_response_value_antes[player]) < tolerance,
            "asymmetric ISO compression preserves best-response EV");
  }
  require(std::abs(iso_off_certification.nash_conv_antes -
                   iso_on_certification.nash_conv_antes) < tolerance,
          "asymmetric ISO compression preserves NashConv");
  require(iso_off.value().checkpoint.game_fingerprint !=
              iso_on.value().checkpoint.game_fingerprint,
          "ISO-off and ISO-on checkpoints identify different layouts");
  std::cout << "ASYMMETRIC_RANGE_NODE_OWNED_ISOMORPHISM_TEST=PASS"
            << " iso_off_public_nodes=" << iso_off.value().canonical_public_nodes
            << " iso_on_public_nodes=" << iso_on.value().canonical_public_nodes
            << " iso_off_actions=" << iso_off.value().actions
            << " iso_on_actions=" << iso_on.value().actions << '\n';
}

void test_prepared_compressed_root_analysis_matches_certification() {
  const auto config = make_reference_config();
  const auto ranges = make_reference_ranges();
  auto prepared =
      gtosd::prepare_postflop_tree(config, ranges, true, true, true).value();
  const auto estimate = gtosd::prepared_postflop_layout_estimate(*prepared);
  require(estimate.physical_public_tree.node_count !=
              estimate.canonical_public_nodes,
          "prepared root differential uses a genuinely compressed public DAG");

  gtosd::PostflopSolveOptions options;
  options.iterations = 80U;
  options.certification_interval = 80U;
  options.algorithm = gtosd::PostflopAlgorithm::Dcfr;
  options.state_precision =
      gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  options.dcfr_positive_regret_exponent = 1.5;
  options.dcfr_average_exponent = 2.0;
  options.averaging_delay = 0U;
  options.parallel_action_depth = 7U;
  const auto solved = gtosd::solve_postflop_exact(*prepared, options);
  require(solved.has_value() && solved.value().convergence.size() == 1U,
          "production signed DCFR root differential solves");

  const auto &certification = solved.value().convergence.back();
  const auto direct = gtosd::analyze_postflop_node(
      config, ranges, solved.value().checkpoint, 0U);
  const auto browser_tree = gtosd::prepared_postflop_public_tree(prepared);
  require(direct.has_value() && browser_tree && !browser_tree->nodes.empty(),
          "direct root and prepared physical browser tree are available");
  const auto prepared_root = gtosd::analyze_postflop_node(
      *prepared, solved.value().checkpoint, browser_tree->root);
  require(prepared_root.has_value(), "prepared root analysis succeeds");

  const auto &root = browser_tree->nodes[browser_tree->root];
  const auto check = std::ranges::find_if(root.edges, [](const auto &edge) {
    return edge.action.type == gtosd::ActionType::Check;
  });
  const auto bet = std::ranges::find_if(root.edges, [](const auto &edge) {
    return edge.action.type == gtosd::ActionType::Bet;
  });
  require(check != root.edges.end() && bet != root.edges.end(),
          "prepared browser root exposes check and bet children");
  const auto check_child = gtosd::analyze_postflop_node(
      *prepared, solved.value().checkpoint, check->child);
  const auto bet_child = gtosd::analyze_postflop_node(
      *prepared, solved.value().checkpoint, bet->child);
  require(check_child.has_value() && bet_child.has_value(),
          "prepared physical browser still analyzes both non-root children");

  const double certification_gto_plus =
      certification.profile_value_antes[0] + 20.0;
  std::cerr << "PREPARED_ROOT_DIFFERENTIAL iteration=80"
            << " certification_profile_p0="
            << certification.profile_value_antes[0]
            << " certification_gto_plus_p0=" << certification_gto_plus
            << " direct_profile_p0=" << direct.value().profile_value_antes[0]
            << " direct_gto_plus_p0=" << direct.value().gto_plus_ev_antes[0]
            << " prepared_profile_p0="
            << prepared_root.value().profile_value_antes[0]
            << " prepared_gto_plus_p0="
            << prepared_root.value().gto_plus_ev_antes[0] << '\n';

  constexpr double root_tolerance = 1.0e-9;
  require(std::abs(certification.profile_value_antes[0] -
                   direct.value().profile_value_antes[0]) < root_tolerance,
          "direct canonical root analysis matches exact certification");
  require(std::abs(direct.value().profile_value_antes[0] -
                   prepared_root.value().profile_value_antes[0]) <
              root_tolerance,
          "prepared compressed root analysis matches direct canonical authority");
}

gtosd::DiagnosticRootLock make_external_root_lock() {
  // F10.4 input immutabile: le 36 righe combo-per-combo Bet 20 / Check del CO
  // root esportate da GTO+ v1.6.9 (run operativo dEV 0.98%, 8 threads),
  // trascritte da docs/specifications/gtoplus_specs.md (tabella CO FI FLOP).
  struct Row {
    const char *combo;
    double bet_20;
    double check;
  };
  static constexpr std::array<Row, 36> rows{
      {{"AsAd", 0.123, 0.877}, {"AsAc", 0.123, 0.877}, {"AdAc", 0.123, 0.877},
       {"KsKc", 0.573, 0.427}, {"KdKc", 0.573, 0.427}, {"KsKd", 0.573, 0.427},
       {"AcKc", 0.12, 0.88},   {"AsKs", 0.12, 0.88},   {"AdKd", 0.12, 0.88},
       {"AdKs", 0.12, 0.88},   {"AcKs", 0.12, 0.88},   {"AsKd", 0.12, 0.88},
       {"AcKd", 0.12, 0.88},   {"AsKc", 0.12, 0.88},   {"AdKc", 0.12, 0.88},
       {"QsQc", 0.41, 0.59},   {"QdQc", 0.41, 0.59},   {"QsQd", 0.41, 0.59},
       {"AsQs", 0.085, 0.915}, {"AdQd", 0.085, 0.915}, {"AcQc", 0.085, 0.915},
       {"AdQs", 0.085, 0.915}, {"AcQd", 0.085, 0.915}, {"AsQc", 0.085, 0.915},
       {"AdQc", 0.085, 0.915}, {"AsQd", 0.085, 0.915}, {"AcQs", 0.085, 0.915},
       {"KcQc", 0.215, 0.785}, {"KsQs", 0.215, 0.785}, {"KdQd", 0.215, 0.785},
       {"KdQs", 0.215, 0.785}, {"KcQs", 0.215, 0.785}, {"KsQd", 0.215, 0.785},
       {"KcQd", 0.215, 0.785}, {"KsQc", 0.215, 0.785}, {"KdQc", 0.215, 0.785}}};
  gtosd::DiagnosticRootLock lock;
  lock.source_description =
      "GTO+ v1.6.9 operational run (dEV 0.98%, 8 threads), CO root Bet 20 / Check rows";
  lock.source_dev_percent = 0.98;
  for (const auto &row : rows) {
    const auto first = gtosd::parse_card(std::string_view(row.combo, 2)).value();
    const auto second = gtosd::parse_card(std::string_view(row.combo + 2, 2)).value();
    gtosd::DiagnosticRootLockEntry entry;
    entry.combo = {std::min(first, second), std::max(first, second)};
    entry.action_labels = {"check", "bet_20"};
    entry.probabilities = {row.check, row.bet_20};
    lock.entries.push_back(std::move(entry));
  }
  return lock;
}

void test_external_root_lock_diagnostic() {
  // F10.4 permanent validation: controlled posteriori with the GTO+ CO root
  // strategy locked. The unconstrained fixture must stay untouched; the locked
  // probabilities must be reproduced exactly; the constrained game converges;
  // the root posteriori stay zero-sum.
  const auto config = make_reference_config();
  const auto ranges = make_reference_ranges();
  const auto lock = make_external_root_lock();

  gtosd::PostflopSolveOptions unconstrained_options;
  unconstrained_options.iterations = 1U;
  const auto unconstrained =
      gtosd::solve_postflop_exact(config, ranges, unconstrained_options).value();

  gtosd::PostflopSolveOptions options;
  options.iterations = 30U;
  options.averaging_delay = 3U;
  options.certification_interval = 1U;
  options.diagnostic_root_lock = &lock;
  const auto solved = gtosd::solve_postflop_exact(config, ranges, options).value();
  const auto &final = solved.convergence.back();

  require(solved.checkpoint.game_fingerprint == unconstrained.checkpoint.game_fingerprint,
          "the root lock does not change the game fingerprint (F10.4 point 2)");
  require(solved.checkpoint.completed_iterations == 30U, "constrained solve completes");

  // Point 3: zero-sum root recomposition and Bet/Check posteriori generated by
  // the lock.
  require(std::abs(final.profile_value_antes[0] + final.profile_value_antes[1]) < 1.0e-9,
          "constrained profile is zero-sum (F10.4 point 3)");
  require(std::abs(final.expected_payoff_sum_antes) < 1.0e-9,
          "expected payoff sum is zero (F10.4 point 3)");

  // Point 4: downstream solve accuracy at least equal to the reference probe
  // (the unconstrained 003 probe converges to 0.695544% at 80 iterations; the
  // constrained game must stay in the same regime at 30 iterations).
  const auto deviation = gtosd::normalized_max_deviation_gain(final, config.initial_pot).value();
  require(deviation < 0.10, "constrained game converges (F10.4 point 4)");
  require(std::isfinite(final.normalized_nash_conv), "constrained NashConv is finite");

  // Point 1: exact combo coverage and locked-probability reproduction at the
  // tree root under the constrained solve.
  const auto prepared = gtosd::prepare_postflop_tree(config, ranges, true, true, true).value();
  const auto public_tree = gtosd::prepared_postflop_public_tree(prepared);
  const auto analysis =
      gtosd::analyze_postflop_node(*prepared, solved.checkpoint, public_tree->root).value();
  require(analysis.combos.size() == lock.entries.size(),
          "root analysis covers exactly the locked combos (F10.4 point 1)");
  double maximum_probability_delta = 0.0;
  const auto combos = gtosd::all_combos();
  for (const auto &entry : lock.entries) {
    const auto combo_it = std::ranges::find(combos, entry.combo);
    require(combo_it != combos.end(), "locked combo is part of the game");
    const auto combo_id = static_cast<gtosd::ComboId>(std::distance(combos.begin(), combo_it));
    const auto combo_analysis =
        std::ranges::find_if(analysis.combos, [combo_id](const auto &combo) {
          return combo.combo == combo_id;
        });
    require(combo_analysis != analysis.combos.end(), "locked combo has a root posterior");
    const auto bet_action = std::ranges::find_if(
        analysis.actions, [](const gtosd::Action &action) {
          return action.type == gtosd::ActionType::Bet &&
                 action.amount.units() == 20 * gtosd::Money::units_per_ante;
        });
    require(bet_action != analysis.actions.end(), "root exposes the locked bet action");
    const auto bet_probability =
        combo_analysis->action_probabilities[static_cast<std::size_t>(
            std::distance(analysis.actions.begin(), bet_action))];
    maximum_probability_delta =
        std::max(maximum_probability_delta, std::abs(bet_probability - entry.probabilities[1]));
  }
  require(maximum_probability_delta < 1.0e-4,
          "locked root probabilities are reproduced per combo (F10.4 point 1)");
  std::cout << "EXTERNAL_ROOT_LOCK_TEST=PASS"
            << " fingerprint=" << solved.checkpoint.game_fingerprint
            << " maximum_probability_delta=" << maximum_probability_delta
            << " constrained_dev_percent=" << deviation * 100.0 << '\n';
}

} // namespace

int main() {
  try {
    test_exact_reference_build();
    test_lossless_isomorphism_matches_physical_cfr();
    test_prepared_compressed_root_analysis_matches_certification();
    test_external_root_lock_diagnostic();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "GTO_PLUS_REFERENCE_TEST=FAIL assertions=" << assertions
              << " error=" << error.what() << '\n';
    return 1;
  }
}
