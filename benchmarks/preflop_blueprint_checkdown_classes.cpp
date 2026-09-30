// Class-level solver for the first tree-building step of MonkerSolver (the
// preflop tree with an empty postflop, every pot that reaches the flop checked
// down to the river) for 2 or 3 seats: vector DCFR over the 81 short-deck hand
// classes with per-seat reach vectors, alternating updates over every seat,
// exact terminal values through per-terminal, per-seat tensors
// (checkdown_classes.hpp), the exact EV and best-response gain of every seat,
// and charts in the MonkerSolver text format named by chart_nodes.
//
// By suit symmetry the class-level DCFR is the combo-level DCFR of
// gtosd_preflop_blueprint_checkdown up to rounding. Heads-up, the terminal
// tensors are preflop_all_in_v1.bin aggregated to 81 x 81 at load, and
// --reference checks the equivalence against the combo-level program's output
// for the same configuration and iterations: every chart row within
// --chart-tolerance, the EVs, the best-response gains and the expected rake
// within --ev-tolerance antes. With 3 seats the terminal tensors come from the
// complete three-player class table (preflop_three_way_v1.bin), with the
// folded hand's cards dead at a 2-way showdown after a fold (--folded-cards
// dead, MonkerSolver's convention) or ignored (--folded-cards ignore: the
// heads-up table weighted by the folded hand's disjoint combos); the 3-way
// step-1 runs use the 3WAY50_donk(_rake) configurations, whose postflop
// settings the checkdown tree never reads. --threads spreads the terminal
// contractions of every traversal (values independent of the thread count).
//
// With 3 players CFR has no Nash guarantee: the quality measure is each seat's
// exact best-response gain against the others' average strategies, in antes
// and in % of the initial pot, with the local gain of every chart node (the
// actor's gain from its best action there alone). With rake the game is not
// zero-sum: the sum of the EVs is minus the expected rake, checked against the
// rake of every terminal weighted by its reach (the check holds without rake
// too, where both are zero).
//
// --lock-charts <folder> [--lock-nodes all|<chart>,<chart>,...] fixes the rows
// of the chosen chart nodes to the folder's charts (monker_chart_lock.hpp, any
// number of seats); the other nodes train. An all-zero chart row of a class
// that reaches the node under the charts (a reach rounded away by the three
// decimals) stays unlocked and is reported. Every class row of every locked
// node must come out as a chart row, a row outside the range or such a zero
// row (locked nodes x 81 in all), and the solver must hold every chart row.
// With every node locked and --iterations 0 the run evaluates the chart set
// alone: every seat's EV and best-response gain inside the checkdown game (how
// exploitable MonkerSolver's preflop is there), the unlocked zero rows
// uniform; --iterations 0 with a partial lock is refused (the other nodes
// would play uniform, untrained), and so is --lock-nodes without
// --lock-charts. --expect-summary <summary.json> [--expect-tolerance antes]
// requires this run's EVs and best-response gains to be those of another run
// of the same configuration within the tolerance (default 0.001 antes): the
// charts of a run locked back on every node reproduce it up to their three
// printed decimals.
#include "checkdown_classes.hpp"
#include "monker_chart_format.hpp"
#include "monker_chart_lock.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace cc = gtosd::checkdown_classes;
namespace mc = gtosd::monker_charts;
namespace pb = gtosd::preflop_blueprint;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// Printing slack of the three-decimal chart format on top of the tolerance.
constexpr double chart_print_slack = 1e-9;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint64_t parse_unsigned(const std::string_view text) {
  std::size_t used = 0;
  const auto value = std::stoull(std::string(text), &used);
  if (used != text.size()) {
    throw std::runtime_error("invalid unsigned value " + std::string(text));
  }
  return value;
}

double parse_double(const std::string_view text) {
  std::size_t used = 0;
  const auto value = std::stod(std::string(text), &used);
  if (used != text.size()) {
    throw std::runtime_error("invalid number " + std::string(text));
  }
  return value;
}

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

std::string join(const std::vector<double> &values) {
  std::ostringstream text;
  text << std::setprecision(12);
  for (std::size_t index = 0; index < values.size(); ++index) {
    text << (index == 0U ? "" : ", ") << values[index];
  }
  return text.str();
}

// A number as the summary prints it (1e-09, not std::to_string's 0.000000).
std::string format_number(const double value) {
  std::ostringstream text;
  text << std::setprecision(12) << value;
  return text.str();
}

std::vector<std::string> split_list(const std::string_view text) {
  std::vector<std::string> items;
  std::size_t start = 0U;
  while (start <= text.size()) {
    const auto comma = text.find(',', start);
    const auto item = text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                         : comma - start);
    if (!item.empty()) {
      items.emplace_back(item);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1U;
  }
  return items;
}

// A preflop decision node's chart, actor, own reach under the average profile
// (combos) and local gain.
struct NodeReport {
  std::string chart;
  std::uint8_t seat{0U};
  double reach_combos{0.0};
  double local_gain{0.0};
};

// Largest differences between this run and the combo-level program's output.
struct Equivalence {
  std::size_t files{0U};
  std::size_t rows{0U};
  std::size_t cells{0U};
  // Cells whose printed values differ at all.
  std::size_t cells_differing{0U};
  double chart{0.0};
  std::string chart_worst;
  double ev{0.0};
  double gain{0.0};
  double rake{0.0};
};

// The reference is the output directory of gtosd_preflop_blueprint_checkdown
// (summary.json and charts/) for the same configuration, tree, iterations and
// discounts. Its summary writes the second EV as minus the first without
// rake (zero-sum), both computed with rake.
Equivalence compare_with_reference(const pb::CompiledGame &game,
                                   const std::filesystem::path &reference,
                                   const std::filesystem::path &charts,
                                   const std::uint64_t iterations, const std::vector<double> &dcfr,
                                   const std::vector<double> &ev, const std::vector<double> &gain) {
  const auto summary = Json::parse(read_file(reference / "summary.json"));
  if (summary.at("config_id").get<std::string>() != game.config().id ||
      summary.at("tree_fingerprint").get<std::string>() != game.fingerprint()) {
    throw std::runtime_error("the reference was solved on another configuration or tree");
  }
  if (summary.at("iterations").get<std::uint64_t>() != iterations ||
      summary.at("dcfr").get<std::vector<double>>() != dcfr) {
    throw std::runtime_error("the reference ran other iterations or discounts");
  }
  const auto reference_ev = summary.at("ev_antes").get<std::vector<double>>();
  const auto reference_gain = summary.at("gain_antes").get<std::vector<double>>();
  if (reference_ev.size() != ev.size() || reference_gain.size() != gain.size()) {
    throw std::runtime_error("the reference has another number of seats");
  }
  Equivalence result;
  for (std::size_t seat = 0; seat < ev.size(); ++seat) {
    result.ev = std::max(result.ev, std::abs(ev[seat] - reference_ev[seat]));
    result.gain = std::max(result.gain, std::abs(gain[seat] - reference_gain[seat]));
  }
  if (game.config().rake.enabled) {
    double sum = 0.0;
    for (const auto value : ev) {
      sum += value;
    }
    result.rake = std::abs(-sum - summary.at("expected_rake_antes").get<double>());
  }
  const auto nodes = mc::chart_nodes(game);
  std::size_t reference_files = 0U;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(reference / "charts")) {
    reference_files += entry.is_regular_file() ? 1U : 0U;
  }
  if (reference_files != nodes.size()) {
    throw std::runtime_error("the reference has " + std::to_string(reference_files) +
                             " chart files, the tree " + std::to_string(nodes.size()));
  }
  for (const auto &node : nodes) {
    const auto ours = mc::read_chart(charts / node.relative);
    const auto theirs = mc::read_chart(reference / "charts" / node.relative);
    if (ours.columns != theirs.columns || ours.rows.size() != theirs.rows.size()) {
      throw std::runtime_error("columns or rows of " + node.relative + " differ");
    }
    ++result.files;
    for (const auto &[label, values] : ours.rows) {
      const auto other = theirs.rows.find(label);
      if (other == theirs.rows.end()) {
        throw std::runtime_error("hand class " + label + " missing in the reference " +
                                 node.relative);
      }
      ++result.rows;
      for (std::size_t column = 0; column < values.size(); ++column) {
        const double difference = std::abs(values[column] - other->second[column]);
        ++result.cells;
        result.cells_differing += difference > 0.0 ? 1U : 0U;
        if (difference > result.chart) {
          result.chart = difference;
          result.chart_worst = node.relative + " " + label + " " + ours.columns[column];
        }
      }
    }
  }
  return result;
}

// Largest differences between this run's EVs and best-response gains and
// those of an expected summary.json (this program's, same configuration,
// tree, seats and folded-card convention).
struct Expectation {
  double ev{0.0};
  double gain{0.0};
};

Expectation compare_with_summary(const pb::CompiledGame &game,
                                 const std::filesystem::path &expected,
                                 const std::optional<std::string> &folded_cards,
                                 const std::vector<double> &ev, const std::vector<double> &gain) {
  const auto summary = Json::parse(read_file(expected));
  if (summary.at("config_id").get<std::string>() != game.config().id ||
      summary.at("tree_fingerprint").get<std::string>() != game.fingerprint()) {
    throw std::runtime_error("the expected summary was solved on another configuration or tree");
  }
  const auto expected_ev = summary.at("ev_antes").get<std::vector<double>>();
  const auto expected_gain = summary.at("gain_antes").get<std::vector<double>>();
  if (expected_ev.size() != ev.size() || expected_gain.size() != gain.size()) {
    throw std::runtime_error("the expected summary has another number of seats");
  }
  if (folded_cards &&
      summary.at("terminal_source").at("folded_cards").get<std::string>() != *folded_cards) {
    throw std::runtime_error("the expected summary used the other folded-card convention");
  }
  Expectation result;
  for (std::size_t seat = 0; seat < ev.size(); ++seat) {
    result.ev = std::max(result.ev, std::abs(ev[seat] - expected_ev[seat]));
    result.gain = std::max(result.gain, std::abs(gain[seat] - expected_gain[seat]));
  }
  return result;
}

int run(const int argc, char **argv) {
  std::filesystem::path config_path;
  std::filesystem::path resources_dir;
  std::filesystem::path output_dir;
  std::filesystem::path reference_dir;
  std::uint64_t iterations = 20'000U;
  std::uint64_t report_every = 1'000U;
  double alpha = 1.5;
  double beta = 0.0;
  double gamma = 2.0;
  double ev_tolerance = 1e-6;
  double chart_tolerance = 1e-3;
  unsigned threads = 1U;
  std::optional<ca::FoldedCards> folded_cards;
  std::filesystem::path lock_dir;
  std::vector<std::string> lock_nodes{"all"};
  bool lock_nodes_given = false;
  std::filesystem::path expected_summary;
  double expect_tolerance = 1e-3;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(name));
    }
    const std::string_view value(argv[++index]);
    if (name == "--config") {
      config_path = value;
    } else if (name == "--resources-dir") {
      resources_dir = value;
    } else if (name == "--output-dir") {
      output_dir = value;
    } else if (name == "--iterations") {
      iterations = parse_unsigned(value);
    } else if (name == "--report-every") {
      report_every = std::max<std::uint64_t>(1U, parse_unsigned(value));
    } else if (name == "--alpha") {
      alpha = parse_double(value);
    } else if (name == "--beta") {
      beta = parse_double(value);
    } else if (name == "--gamma") {
      gamma = parse_double(value);
    } else if (name == "--reference") {
      reference_dir = value;
    } else if (name == "--ev-tolerance") {
      ev_tolerance = parse_double(value);
    } else if (name == "--chart-tolerance") {
      chart_tolerance = parse_double(value);
    } else if (name == "--threads") {
      const auto parsed = parse_unsigned(value);
      if (parsed == 0U || parsed > 256U) {
        throw std::runtime_error("--threads takes 1 to 256");
      }
      threads = static_cast<unsigned>(parsed);
    } else if (name == "--folded-cards") {
      if (value == "dead") {
        folded_cards = ca::FoldedCards::Dead;
      } else if (value == "ignore") {
        folded_cards = ca::FoldedCards::Ignored;
      } else {
        throw std::runtime_error("--folded-cards takes dead or ignore");
      }
    } else if (name == "--lock-charts") {
      lock_dir = value;
    } else if (name == "--lock-nodes") {
      lock_nodes = split_list(value);
      lock_nodes_given = true;
    } else if (name == "--expect-summary") {
      expected_summary = value;
    } else if (name == "--expect-tolerance") {
      expect_tolerance = parse_double(value);
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (config_path.empty() || resources_dir.empty() || output_dir.empty()) {
    throw std::runtime_error("--config, --resources-dir and --output-dir are required");
  }
  if (iterations == 0U && lock_dir.empty()) {
    throw std::runtime_error("--iterations must be positive (0 only evaluates --lock-charts)");
  }
  if (lock_nodes_given && lock_dir.empty()) {
    throw std::runtime_error("--lock-nodes needs --lock-charts");
  }
  const auto started = Clock::now();
  const auto config = pb::parse_game_config_json(read_file(config_path));
  if (!config) {
    throw std::runtime_error(std::string("configuration rejected: ") +
                             pb::config_error_name(config.error()));
  }
  const auto seats = static_cast<std::size_t>(config.value().player_count);
  if (seats < 2U || seats > cc::maximum_seats) {
    throw std::runtime_error("the class-level checkdown solver takes 2 or 3 players");
  }
  pb::CompileOptions options;
  options.checkdown_at_flop = true;
  const auto compiled = pb::CompiledGame::compile(config.value(), options);
  if (!compiled) {
    throw std::runtime_error(std::string("compile failed: ") +
                             pb::game_model_error_name(compiled.error()));
  }
  const auto &game = compiled.value();
  if (!game.postflop_entries().empty()) {
    throw std::runtime_error("the checkdown tree still has postflop entries");
  }
  cc::check_terminal_rake(game);
  if (seats == 2U && folded_cards) {
    throw std::runtime_error("--folded-cards needs a 3-player game");
  }
  if (seats == 3U && !reference_dir.empty()) {
    throw std::runtime_error("--reference needs a 2-player game (the combo-level program)");
  }
  // The lock is read before the tables load. Every class row of every locked
  // node is a chart row, a row outside the range or a zero row left unlocked:
  // a node whose rows went missing (a seat skipped, a row on another node)
  // breaks the count.
  const auto chart_count = mc::chart_nodes(game).size();
  std::optional<mc::ChartLock> lock;
  std::size_t lock_rows = 0U;
  if (!lock_dir.empty()) {
    lock = mc::chart_lock_seats(game, lock_dir, lock_nodes, true);
    lock_rows =
        static_cast<std::size_t>(lock->chart_rows) + lock->outside_range_rows + lock->fallback_rows;
    if (lock_rows != lock->files.size() * cc::class_count) {
      throw std::runtime_error("the lock classified " + std::to_string(lock_rows) + " rows of " +
                               std::to_string(lock->files.size()) + " charts, not " +
                               std::to_string(cc::class_count) + " per chart");
    }
    if (iterations == 0U && lock->files.size() != chart_count) {
      throw std::runtime_error(
          "--iterations 0 evaluates a complete chart set: --lock-nodes locks " +
          std::to_string(lock->files.size()) + " of " + std::to_string(chart_count) +
          " chart nodes and the others would play uniform (lock all, or train them)");
    }
    std::cout << "lock: " << lock->files.size() << " of " << chart_count << " chart nodes from "
              << lock_dir.generic_string() << ": " << lock_rows << " rows (" << lock->files.size()
              << " x " << cc::class_count << ") = " << lock->chart_rows << " chart rows + "
              << lock->outside_range_rows << " rows outside the range + " << lock->fallback_rows
              << " zero rows reached under the charts (" << lock->fallback_reach_combos
              << " combos) left unlocked\n";
  }
  auto table = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
  if (!table) {
    throw std::runtime_error("cannot load preflop_all_in_v1.bin");
  }
  const std::string terminal_source = "preflop_all_in_v1.bin";
  const std::string terminal_fingerprint = table.value().fingerprint();
  const auto convention = folded_cards.value_or(ca::FoldedCards::Dead);
  // With 3 seats: the three-player table's fingerprint, the tensors built.
  std::string three_way_fingerprint;
  const auto terminals = [&] {
    if (seats == 2U) {
      return cc::heads_up_terminals(game, table.value());
    }
    const auto three_way =
        ca::ThreeWayTable::load(resources_dir / std::string(ca::three_way_table_file_name));
    if (!three_way) {
      throw std::runtime_error("cannot load the complete " +
                               std::string(ca::three_way_table_file_name));
    }
    three_way_fingerprint = three_way.value().fingerprint();
    return cc::three_way_terminals(game, three_way.value(), table.value(), convention);
  }();
  const double load_seconds = seconds_since(started);

  cc::ClassSolver solver(game, terminals, alpha, beta, gamma, threads);
  if (lock) {
    for (const auto &row : lock->lock.rows) {
      solver.lock_row(row.node, row.hand_class, row.frequencies);
    }
    if (solver.locked_rows() != lock->chart_rows) {
      throw std::runtime_error("the solver holds " + std::to_string(solver.locked_rows()) +
                               " locked rows, the charts give " + std::to_string(lock->chart_rows));
    }
  }
  const double initial_pot =
      static_cast<double>(config.value().ante.units() *
                              static_cast<std::int64_t>(config.value().player_count) +
                          config.value().button_blind.units()) /
      cc::units_per_ante;
  std::ostringstream trajectory;
  bool first_report = true;
  double iteration_seconds = 0.0;
  std::vector<double> ev(seats, 0.0);
  std::vector<double> gain(seats, 0.0);
  const auto largest = [](const std::vector<double> &values) {
    return *std::max_element(values.begin(), values.end());
  };
  const auto report = [&](const std::uint64_t iteration) {
    for (std::size_t seat = 0; seat < seats; ++seat) {
      const auto hero = static_cast<std::uint8_t>(seat);
      ev[seat] = solver.value(hero, cc::Mode::Average);
      gain[seat] = solver.value(hero, cc::Mode::BestResponse) - ev[seat];
    }
    const double seconds = seconds_since(started);
    std::cerr << "iteration " << iteration << " gain " << join(gain) << " a ("
              << 100.0 * largest(gain) / initial_pot << " % of the pot) ev " << join(ev)
              << " seconds " << seconds << '\n';
    trajectory << (first_report ? "    " : ",\n    ") << "{\"iteration\": " << iteration
               << ", \"gain\": [" << join(gain) << "], \"seconds\": " << seconds << "}";
    first_report = false;
  };
  for (std::uint64_t iteration = 1; iteration <= iterations; ++iteration) {
    const auto iteration_started = Clock::now();
    solver.iterate(iteration);
    iteration_seconds += seconds_since(iteration_started);
    if (iteration % report_every == 0U || iteration == iterations) {
      report(iteration);
    }
  }
  if (iterations == 0U) {
    report(0U);
  }
  // Minus the sum of the EVs is the expected rake per hand (zero without
  // rake); the traversal is exact, so it equals the rake by reach.
  double ev_sum = 0.0;
  for (const auto value : ev) {
    ev_sum += value;
  }
  const auto own_reach = solver.own_reach();
  const double direct_rake = cc::expected_rake_by_reach(game, terminals, own_reach);
  if (std::abs(direct_rake + ev_sum) > 1e-9) {
    throw std::runtime_error("the expected rake by reach differs from minus the sum of the EVs");
  }
  const auto charts_dir = output_dir / "charts";
  const auto written = cc::write_class_charts(game, solver, charts_dir);
  std::vector<double> gain_percent(seats, 0.0);
  for (std::size_t seat = 0; seat < seats; ++seat) {
    gain_percent[seat] = 100.0 * gain[seat] / initial_pot;
  }
  // Every chart node with its actor's own reach and local gain (from the
  // last report's average traversals), largest gain first.
  const auto masses = cc::class_masses();
  std::vector<NodeReport> node_reports;
  for (const auto &chart : mc::chart_nodes(game)) {
    NodeReport entry;
    entry.chart = chart.relative;
    entry.seat = game.nodes()[chart.node].actor;
    for (std::size_t hand_class = 0; hand_class < cc::class_count; ++hand_class) {
      entry.reach_combos += masses[hand_class] * own_reach[chart.node][entry.seat][hand_class];
    }
    entry.local_gain = solver.local_gain(chart.node);
    node_reports.push_back(std::move(entry));
  }
  std::stable_sort(node_reports.begin(), node_reports.end(),
                   [](const NodeReport &left, const NodeReport &right) {
                     return left.local_gain > right.local_gain;
                   });
  const double seconds_per_iteration =
      iterations == 0U ? 0.0 : iteration_seconds / static_cast<double>(iterations);
  const std::vector<double> dcfr{alpha, beta, gamma};
  const bool compared = !reference_dir.empty();
  Equivalence equivalence;
  if (compared) {
    equivalence =
        compare_with_reference(game, reference_dir, charts_dir, iterations, dcfr, ev, gain);
  }
  const bool equivalent = equivalence.chart <= chart_tolerance + chart_print_slack &&
                          equivalence.ev <= ev_tolerance && equivalence.gain <= ev_tolerance &&
                          equivalence.rake <= ev_tolerance;
  const std::string convention_name = convention == ca::FoldedCards::Dead ? "dead" : "ignore";
  const bool expecting = !expected_summary.empty();
  Expectation expectation;
  if (expecting) {
    expectation = compare_with_summary(
        game, expected_summary,
        seats == 3U ? std::optional<std::string>(convention_name) : std::nullopt, ev, gain);
  }
  const bool as_expected =
      expectation.ev <= expect_tolerance && expectation.gain <= expect_tolerance;
  const double seconds = seconds_since(started);
  const auto &rake = config.value().rake;
  std::ofstream summary(output_dir / "summary.json", std::ios::binary);
  summary << std::setprecision(12) << "{\n"
          << "  \"schema\": \"gtosd.preflop_blueprint_checkdown_classes.v1\",\n"
          << "  \"config_id\": \"" << config.value().id << "\",\n"
          << "  \"tree_fingerprint\": \"" << game.fingerprint() << "\",\n"
          << "  \"seats\": " << seats << ",\n";
  if (seats == 2U) {
    summary << "  \"terminal_source\": {\"file\": \"" << terminal_source
            << "\", \"fingerprint\": \"" << terminal_fingerprint << "\"},\n";
  } else {
    summary << "  \"terminal_source\": {\"file\": \"" << ca::three_way_table_file_name
            << "\", \"fingerprint\": \"" << three_way_fingerprint << "\", \"heads_up_file\": \""
            << terminal_source << "\", \"heads_up_fingerprint\": \"" << terminal_fingerprint
            << "\", \"folded_cards\": \"" << convention_name << "\"},\n";
  }
  summary << "  \"threads\": " << threads << ",\n"
          << "  \"nodes\": " << game.nodes().size() << ",\n"
          << "  \"terminals\": {\"folds\": " << terminals.fold_terminals
          << ", \"showdowns\": " << terminals.showdown_terminals
          << ", \"tensors\": " << terminals.tensors.size()
          << ", \"tensor_bytes\": " << terminals.tensor_bytes() << "},\n"
          << "  \"iterations\": " << iterations << ",\n"
          << "  \"dcfr\": [" << join(dcfr) << "],\n"
          << "  \"initial_pot_antes\": " << initial_pot << ",\n"
          << "  \"ev_antes\": [" << join(ev) << "],\n";
  if (rake.enabled) {
    summary << "  \"rake\": {\"basis_points\": " << rake.percentage.basis_points()
            << ", \"cap_antes\": " << static_cast<double>(rake.cap.units()) / cc::units_per_ante
            << ", \"no_flop_no_drop\": " << (rake.no_flop_no_drop ? "true" : "false")
            << ", \"minimum_pot_antes\": "
            << static_cast<double>(rake.minimum_pot.units()) / cc::units_per_ante << "},\n";
  }
  summary << "  \"expected_rake_antes\": " << -ev_sum << ",\n"
          << "  \"expected_rake_by_reach_antes\": " << direct_rake << ",\n"
          << "  \"gain_antes\": [" << join(gain) << "],\n"
          << "  \"gain_pot_percent\": [" << join(gain_percent) << "],\n"
          << "  \"max_gain_pot_percent\": " << largest(gain_percent) << ",\n"
          << "  \"seconds\": " << seconds << ",\n"
          << "  \"load_seconds\": " << load_seconds << ",\n"
          << "  \"iteration_seconds\": " << iteration_seconds << ",\n"
          << "  \"seconds_per_iteration\": " << seconds_per_iteration << ",\n"
          << "  \"charts\": " << written.size() << ",\n";
  if (lock) {
    summary << "  \"lock\": {\"charts_dir\": " << Json(lock_dir.generic_string()).dump()
            << ", \"files\": " << lock->files.size() << ", \"chart_nodes\": " << chart_count
            << ", \"rows\": " << lock_rows << ", \"chart_rows\": " << lock->chart_rows
            << ", \"outside_range_rows\": " << lock->outside_range_rows
            << ", \"fallback_rows\": " << lock->fallback_rows
            << ", \"fallback_reach_combos\": " << lock->fallback_reach_combos << "},\n";
  }
  summary << "  \"chart_nodes\": [\n";
  for (std::size_t index = 0; index < node_reports.size(); ++index) {
    const auto &entry = node_reports[index];
    summary << "    {\"chart\": " << Json(entry.chart).dump()
            << ", \"seat\": " << static_cast<unsigned>(entry.seat)
            << ", \"own_reach_combos\": " << entry.reach_combos
            << ", \"local_gain_antes\": " << entry.local_gain << "}"
            << (index + 1U < node_reports.size() ? ",\n" : "\n");
  }
  summary << "  ],\n";
  if (compared) {
    summary << "  \"equivalence\": {\"reference\": " << Json(reference_dir.generic_string()).dump()
            << ", \"files\": " << equivalence.files << ", \"rows\": " << equivalence.rows
            << ", \"cells\": " << equivalence.cells
            << ", \"cells_differing\": " << equivalence.cells_differing
            << ", \"chart_max_difference\": " << equivalence.chart
            << ", \"ev_max_difference_antes\": " << equivalence.ev
            << ", \"gain_max_difference_antes\": " << equivalence.gain
            << ", \"expected_rake_difference_antes\": " << equivalence.rake
            << ", \"chart_tolerance\": " << chart_tolerance
            << ", \"ev_tolerance_antes\": " << ev_tolerance
            << ", \"passed\": " << (equivalent ? "true" : "false") << "},\n";
  }
  if (expecting) {
    summary << "  \"expected_summary\": {\"file\": "
            << Json(expected_summary.generic_string()).dump()
            << ", \"ev_max_difference_antes\": " << expectation.ev
            << ", \"gain_max_difference_antes\": " << expectation.gain
            << ", \"tolerance_antes\": " << expect_tolerance
            << ", \"passed\": " << (as_expected ? "true" : "false") << "},\n";
  }
  summary << "  \"trajectory\": [\n" << trajectory.str() << "\n  ]\n}\n";
  summary.close();
  if (!summary) {
    throw std::runtime_error("cannot write " + (output_dir / "summary.json").string());
  }
  std::cout << std::setprecision(12) << "EV [" << join(ev) << "] antes, expected rake -(sum EV) "
            << -ev_sum << ", by reach " << direct_rake << " antes\n"
            << "gain [" << join(gain) << "] antes, [" << join(gain_percent) << "] % of the pot; "
            << seconds_per_iteration << " s per iteration\n";
  std::cout << "largest local gains:";
  for (std::size_t index = 0; index < std::min<std::size_t>(5U, node_reports.size()); ++index) {
    std::cout << (index == 0U ? " " : "; ") << node_reports[index].chart << ' '
              << node_reports[index].local_gain << " a";
  }
  std::cout << '\n';
  if (compared) {
    std::cout << "equivalence: " << equivalence.files << " charts, " << equivalence.rows
              << " rows, largest chart difference " << equivalence.chart << " ("
              << equivalence.cells_differing << " of " << equivalence.cells
              << " cells differ; worst " << equivalence.chart_worst << "), EV " << equivalence.ev
              << ", gain " << equivalence.gain << ", expected rake " << equivalence.rake
              << " antes\n";
    if (!equivalent) {
      throw std::runtime_error("not equivalent to the reference within chart tolerance " +
                               format_number(chart_tolerance) + " and EV tolerance " +
                               format_number(ev_tolerance) + " antes");
    }
  }
  if (expecting) {
    std::cout << "expected summary " << expected_summary.generic_string()
              << ": largest EV difference " << expectation.ev << ", gain difference "
              << expectation.gain << " antes\n";
    if (!as_expected) {
      throw std::runtime_error("the EVs or gains differ from the expected summary by more than " +
                               format_number(expect_tolerance) + " antes");
    }
  }
  std::cout << "PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES=PASS seats=" << seats
            << " charts=" << written.size() << " max_gain_pot_percent=" << largest(gain_percent)
            << (compared ? " equivalence=PASS" : "") << (expecting ? " expected_summary=PASS" : "")
            << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES=FAIL " << error.what() << '\n';
    return 1;
  }
}
