#include "gtosd/core/cards.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/isomorphism/isomorphism.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/solver.hpp"
#include "gtosd/tree/tree.hpp"
#include "gtosd/version.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::optional<std::uint64_t> parse_u64(const std::string_view text) {
  std::uint64_t value = 0;
  const auto *const begin = text.data();
  const auto *const end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}

int inspect_tree(const char *const path, const std::string_view maximum_nodes_text) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "tree-inspect failed: cannot_open_config\n";
    return 1;
  }
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  if (!config) {
    std::cerr << "tree-inspect failed: " << gtosd::tree_config_error_name(config.error()) << '\n';
    return 1;
  }

  gtosd::TreeBuildOptions options;
  if (!maximum_nodes_text.empty()) {
    const auto *const begin = maximum_nodes_text.data();
    const auto *const end = begin + maximum_nodes_text.size();
    const auto parsed = std::from_chars(begin, end, options.maximum_nodes);
    if (parsed.ec != std::errc{} || parsed.ptr != end || options.maximum_nodes == 0U) {
      std::cerr << "tree-inspect failed: invalid_maximum_nodes\n";
      return 1;
    }
  }

  const auto estimate = gtosd::estimate_public_tree(config.value(), options);
  if (!estimate) {
    std::cerr << "tree-inspect preflight failed: " << gtosd::tree_error_name(estimate.error())
              << '\n';
    return 1;
  }
  const auto tree = gtosd::build_public_tree(config.value(), options);
  if (!tree) {
    std::cerr << "tree-inspect failed: " << gtosd::tree_error_name(tree.error()) << '\n';
    return 1;
  }
  const auto &stats = tree.value().stats;
  std::cout << "GTOSD_PUBLIC_TREE_1\n"
            << "preflight_nodes=" << estimate.value().node_count
            << " preflight_eager_bytes=" << estimate.value().estimated_eager_bytes << '\n'
            << "nodes=" << stats.node_count << " edges=" << stats.edge_count
            << " max_depth=" << stats.maximum_depth << '\n'
            << "decision_nodes=" << stats.decision_nodes << " chance_nodes=" << stats.chance_nodes
            << " terminal_fold_nodes=" << stats.terminal_fold_nodes
            << " terminal_showdown_nodes=" << stats.terminal_showdown_nodes << '\n'
            << "chance_edges=" << stats.chance_edges
            << " estimated_eager_bytes=" << stats.estimated_eager_bytes << '\n'
            << "betting_tree_hash=" << tree.value().betting_tree_hash << '\n';
  return 0;
}

std::string format_board(const std::uint64_t board_mask) {
  std::ostringstream output;
  bool first = true;
  for (std::uint8_t index = 0; index < 36U; ++index) {
    if ((board_mask & (std::uint64_t{1} << index)) == 0U) {
      continue;
    }
    if (!first) {
      output << ',';
    }
    first = false;
    output << gtosd::format_card(gtosd::CardId::from_index(index).value());
  }
  return output.str();
}

int audit_isomorphism(const char *const path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "isomorphism-audit failed: cannot_open_config\n";
    return 1;
  }
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  if (!config) {
    std::cerr << "isomorphism-audit failed: " << gtosd::tree_config_error_name(config.error())
              << '\n';
    return 1;
  }
  const std::vector<gtosd::CardId> flop(config.value().flop.begin(), config.value().flop.end());
  const auto board_mask = gtosd::card_mask(flop);
  if (!board_mask) {
    std::cerr << "isomorphism-audit failed: invalid_board\n";
    return 1;
  }

  gtosd::CanonicalStateInput state;
  state.game_version = gtosd::PostflopTreeConfig::current_version;
  const auto public_state =
      gtosd::make_hu_postflop_state(gtosd::Street::Flop, config.value().initial_pot,
                                    config.value().effective_stack, board_mask.value());
  if (!public_state) {
    std::cerr << "isomorphism-audit failed: invalid_public_state\n";
    return 1;
  }
  state.public_state = public_state.value();

  gtosd::CanonicalKeyCache cache;
  const auto canonical = cache.canonicalize(state);
  const auto cached = cache.canonicalize(state);
  const auto orbit = gtosd::audit_orbit(state);
  if (!canonical || !cached || !orbit) {
    const auto error = canonical ? (cached ? orbit.error() : cached.error()) : canonical.error();
    std::cerr << "isomorphism-audit failed: " << gtosd::isomorphism_error_name(error) << '\n';
    return 1;
  }

  std::set<std::string> distinct;
  std::size_t canonical_representatives = 0;
  std::cout << "GTOSD_ISOMORPHISM_AUDIT_1\n"
            << "input_board=" << format_board(state.public_state.board_mask) << '\n'
            << "permutations=" << orbit.value().size() << '\n'
            << "physical_to_canonical="
            << gtosd::permutation_name(canonical.value().physical_to_canonical) << '\n'
            << "canonical_to_physical="
            << gtosd::permutation_name(canonical.value().canonical_to_physical) << '\n';
  for (std::size_t index = 0; index < orbit.value().size(); ++index) {
    const auto &entry = orbit.value()[index];
    const auto transformed =
        gtosd::transform_card_mask(state.public_state.board_mask, entry.permutation).value();
    distinct.insert(entry.serialized_state);
    canonical_representatives += entry.is_canonical ? 1U : 0U;
    std::cout << "orbit[" << index << "]=" << gtosd::permutation_name(entry.permutation)
              << " board=" << format_board(transformed)
              << " canonical=" << (entry.is_canonical ? "yes" : "no") << '\n';
  }
  std::cout << "distinct_physical_representations=" << distinct.size() << '\n'
            << "canonical_representatives=" << canonical_representatives << '\n'
            << "cache_queries=" << cache.stats().queries << " cache_hits=" << cache.stats().hits
            << " cache_misses=" << cache.stats().misses
            << " hash_collisions=" << cache.stats().hash_collisions
            << " hit_rate=" << cache.hit_rate() << '\n';
  return 0;
}

std::optional<gtosd::SolverAlgorithm> parse_algorithm(const std::string_view text) {
  if (text == "cfr") {
    return gtosd::SolverAlgorithm::VanillaCfr;
  }
  if (text == "cfr+") {
    return gtosd::SolverAlgorithm::CfrPlus;
  }
  if (text == "linear") {
    return gtosd::SolverAlgorithm::LinearCfr;
  }
  if (text == "dcfr") {
    return gtosd::SolverAlgorithm::Dcfr;
  }
  if (text == "mccfr") {
    return gtosd::SolverAlgorithm::ExternalSamplingMccfr;
  }
  return std::nullopt;
}

std::optional<gtosd::FiniteGame> make_lab_game(const std::string_view name) {
  if (name == "matching") {
    return gtosd::make_matching_pennies_game();
  }
  if (name == "kuhn") {
    return gtosd::make_kuhn_poker_game();
  }
  if (name == "leduc") {
    return gtosd::make_leduc_poker_game();
  }
  if (name == "short-deck-toy") {
    const auto game = gtosd::make_short_deck_river_toy_game();
    return game ? std::optional<gtosd::FiniteGame>{game.value()} : std::nullopt;
  }
  if (name == "short-deck-rake-toy") {
    const auto game = gtosd::make_short_deck_river_toy_game(0.05);
    return game ? std::optional<gtosd::FiniteGame>{game.value()} : std::nullopt;
  }
  return std::nullopt;
}

struct SolverLabArguments {
  std::string_view game;
  std::string_view algorithm;
  std::string_view iterations;
  std::string_view seed;
  std::string_view threads;
};

int run_solver_lab(const SolverLabArguments arguments) {
  const auto game = make_lab_game(arguments.game);
  const auto algorithm = parse_algorithm(arguments.algorithm);
  const auto iterations = parse_u64(arguments.iterations);
  const auto seed = arguments.seed.empty() ? std::optional<std::uint64_t>{0x47544f5344463541ULL}
                                           : parse_u64(arguments.seed);
  const auto threads =
      arguments.threads.empty() ? std::optional<std::uint64_t>{1U} : parse_u64(arguments.threads);
  if (!game || !algorithm || !iterations || *iterations == 0U || !seed || !threads ||
      *threads > 8U) {
    std::cerr << "solver-lab failed: invalid_argument\n";
    return 2;
  }

  gtosd::SolverConfig config;
  config.algorithm = *algorithm;
  config.iterations = *iterations;
  config.seed = *seed;
  config.thread_count = static_cast<std::uint32_t>(*threads);
  config.averaging_delay = *algorithm == gtosd::SolverAlgorithm::CfrPlus
                               ? std::min<std::uint64_t>(100, *iterations / 10)
                               : 0U;
  const auto summary = gtosd::validate_finite_game(*game);
  if (!summary) {
    std::cerr << "solver-lab failed: " << gtosd::solver_error_name(summary.error()) << '\n';
    return 1;
  }

  const auto started = std::chrono::steady_clock::now();
  const auto certified = gtosd::solve_with_certification(
      *game, config, std::max<std::uint64_t>(1U, *iterations / 10U));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  if (!certified) {
    std::cerr << "solver-lab failed: " << gtosd::solver_error_name(certified.error()) << '\n';
    return 1;
  }
  const auto metrics = gtosd::calculate_nash_conv(*game, certified.value().solve.average_strategy);
  const auto checkpoint = gtosd::serialize_solver_checkpoint(certified.value().solve.checkpoint);
  if (!metrics || !checkpoint) {
    const auto error = metrics ? checkpoint.error() : metrics.error();
    std::cerr << "solver-lab failed: " << gtosd::solver_error_name(error) << '\n';
    return 1;
  }
  const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
  const double iterations_per_second =
      elapsed_seconds > 0.0 ? static_cast<double>(*iterations) / elapsed_seconds : 0.0;

  std::cout << "GTOSD_SOLVER_LAB_1\n"
            << "game=" << game->game_id << " fingerprint=" << summary.value().fingerprint << '\n'
            << "nodes=" << summary.value().nodes << " infosets=" << summary.value().information_sets
            << " max_depth=" << summary.value().maximum_depth << '\n'
            << "algorithm=" << gtosd::solver_algorithm_name(*algorithm) << " seed=" << *seed
            << " threads=" << *threads << " iterations=" << *iterations << '\n';
  for (const auto &point : certified.value().convergence) {
    std::cout << "curve iteration=" << point.iteration << " ev_co=" << point.profile_value[0]
              << " ev_btn=" << point.profile_value[1] << " nash_conv=" << point.nash_conv << '\n';
  }
  std::cout << "final_ev_co=" << metrics.value().profile_value[0]
            << " final_ev_btn=" << metrics.value().profile_value[1]
            << " payoff_sum=" << metrics.value().expected_payoff_sum << '\n'
            << "br_co=" << metrics.value().best_response_value[0]
            << " br_btn=" << metrics.value().best_response_value[1]
            << " nash_conv=" << metrics.value().nash_conv
            << " normalized_nash_conv=" << metrics.value().normalized_nash_conv << '\n'
            << "zero_sum_exploitability=";
  if (std::isnan(metrics.value().zero_sum_exploitability)) {
    std::cout << "not_applicable_general_sum\n";
  } else {
    std::cout << metrics.value().zero_sum_exploitability << '\n';
  }
  std::cout << "elapsed_seconds=" << elapsed_seconds
            << " iterations_per_second=" << iterations_per_second
            << " traversed_nodes=" << certified.value().solve.traversed_nodes
            << " checkpoint_bytes=" << checkpoint.value().size()
            << " normalization_error=" << certified.value().solve.maximum_normalization_error
            << '\n';
  return 0;
}

struct DcfrSweepArguments {
  std::string_view game;
  std::string_view iterations;
};

int run_dcfr_sweep(const DcfrSweepArguments arguments) {
  const auto game = make_lab_game(arguments.game);
  const auto iterations = parse_u64(arguments.iterations);
  if (!game || !iterations || *iterations == 0U) {
    std::cerr << "dcfr-sweep failed: invalid_argument\n";
    return 2;
  }
  constexpr std::array<gtosd::DcfrParameters, 6> candidates{{
      {1.0, 0.0, 1.0},
      {1.5, 0.0, 2.0},
      {2.0, 0.0, 2.0},
      {1.5, -0.5, 2.0},
      {1.5, 0.5, 2.0},
      {1.5, 0.0, 3.0},
  }};

  std::cout << "GTOSD_DCFR_SWEEP_1\n"
            << "game=" << game->game_id << " iterations=" << *iterations
            << " candidates=" << candidates.size() << '\n';
  for (const auto &parameters : candidates) {
    gtosd::SolverConfig config;
    config.algorithm = gtosd::SolverAlgorithm::Dcfr;
    config.iterations = *iterations;
    config.dcfr = parameters;
    const auto started = std::chrono::steady_clock::now();
    const auto solved = gtosd::solve_finite_game(*game, config);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!solved) {
      std::cerr << "dcfr-sweep failed: " << gtosd::solver_error_name(solved.error()) << '\n';
      return 1;
    }
    const auto metrics = gtosd::calculate_nash_conv(*game, solved.value().average_strategy);
    if (!metrics) {
      std::cerr << "dcfr-sweep failed: " << gtosd::solver_error_name(metrics.error()) << '\n';
      return 1;
    }
    std::cout << "alpha=" << parameters.positive_regret_exponent
              << " beta=" << parameters.negative_regret_exponent
              << " gamma=" << parameters.strategy_exponent
              << " ev_co=" << metrics.value().profile_value[0]
              << " nash_conv=" << metrics.value().nash_conv << " elapsed_seconds=" << elapsed
              << '\n';
  }
  return 0;
}

void print_usage() {
  std::cout << "Usage:\n"
            << "  gto_cli self-check\n"
            << "  gto_cli tree-inspect <config.json> [maximum_nodes]\n"
            << "  gto_cli isomorphism-audit <config.json>\n"
            << "  gto_cli solver-lab <game> <algorithm> <iterations> [seed] [threads]\n"
            << "  gto_cli dcfr-sweep <game> <iterations>\n"
            << "    game: matching|kuhn|leduc|short-deck-toy|short-deck-rake-toy\n"
            << "    algorithm: cfr|cfr+|linear|dcfr|mccfr\n";
}

} // namespace

int run_cli(const int argc, const char *const argv[]) {
  if (argc == 2 && std::string_view(argv[1]) == "self-check") {
    const auto ante = gtosd::Money::from_antes(1).value();
    const auto stack = gtosd::Money::from_antes(40).value();
    const auto state = gtosd::make_hu_preflop_state(stack, ante);
    if (!state) {
      std::cerr << "self-check failed\n";
      return 1;
    }
    std::cout << "GTOSD " << gtosd::api_version_string << '\n'
              << "deck_cards=36 combos=" << gtosd::all_combos().size() << " hand_classes=81\n"
              << "positions=CO,BTN actor=CO root_pot_units=" << state.value().pot.units()
              << " call_units=" << gtosd::amount_to_call(state.value(), 0).units() << '\n';
    return 0;
  }
  if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "tree-inspect") {
    return inspect_tree(argv[2], argc == 4 ? std::string_view(argv[3]) : std::string_view{});
  }
  if (argc == 3 && std::string_view(argv[1]) == "isomorphism-audit") {
    return audit_isomorphism(argv[2]);
  }
  if (argc >= 5 && argc <= 7 && std::string_view(argv[1]) == "solver-lab") {
    return run_solver_lab({argv[2], argv[3], argv[4],
                           argc >= 6 ? std::string_view(argv[5]) : std::string_view{},
                           argc == 7 ? std::string_view(argv[6]) : std::string_view{}});
  }
  if (argc == 4 && std::string_view(argv[1]) == "dcfr-sweep") {
    return run_dcfr_sweep({argv[2], argv[3]});
  }
  print_usage();
  return argc == 1 ? 0 : 2;
}

int main(const int argc, const char *const argv[]) {
  try {
    return run_cli(argc, argv);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "gto_cli failed: internal_exception: %s\n", error.what());
    return 1;
  } catch (...) {
    std::fputs("gto_cli failed: unknown_internal_exception\n", stderr);
    return 1;
  }
}
