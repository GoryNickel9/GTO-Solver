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
// within --ev-tolerance antes. The 3-way terminals (three-player class table,
// folded cards dead or ignored) are phase 2b part 2.
//
// With 3 players CFR has no Nash guarantee: the quality measure is each seat's
// exact best-response gain against the others' average strategies, in antes
// and in % of the initial pot. With rake the game is not zero-sum: the sum of
// the EVs is minus the expected rake, checked against the rake of every
// terminal weighted by its reach (the check holds without rake too, where
// both are zero).
#include "checkdown_classes.hpp"
#include "monker_chart_format.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
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
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (config_path.empty() || resources_dir.empty() || output_dir.empty()) {
    throw std::runtime_error("--config, --resources-dir and --output-dir are required");
  }
  if (iterations == 0U) {
    throw std::runtime_error("--iterations must be positive");
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
  if (seats != 2U) {
    throw std::runtime_error(
        "3-player terminal tensors are not built yet (phase 2b part 2, three-player class table)");
  }
  auto table = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
  if (!table) {
    throw std::runtime_error("cannot load preflop_all_in_v1.bin");
  }
  const auto terminals = cc::heads_up_terminals(game, table.value());
  const std::string terminal_source = "preflop_all_in_v1.bin";
  const std::string terminal_fingerprint = table.value().fingerprint();
  const double load_seconds = seconds_since(started);

  cc::ClassSolver solver(game, terminals, alpha, beta, gamma);
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
  for (std::uint64_t iteration = 1; iteration <= iterations; ++iteration) {
    const auto iteration_started = Clock::now();
    solver.iterate(iteration);
    iteration_seconds += seconds_since(iteration_started);
    if (iteration % report_every == 0U || iteration == iterations) {
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
    }
  }
  // Minus the sum of the EVs is the expected rake per hand (zero without
  // rake); the traversal is exact, so it equals the rake by reach.
  double ev_sum = 0.0;
  for (const auto value : ev) {
    ev_sum += value;
  }
  const double direct_rake = cc::expected_rake_by_reach(game, terminals, solver.own_reach());
  if (std::abs(direct_rake + ev_sum) > 1e-9) {
    throw std::runtime_error("the expected rake by reach differs from minus the sum of the EVs");
  }
  const auto charts_dir = output_dir / "charts";
  const auto written = cc::write_class_charts(game, solver, charts_dir);
  std::vector<double> gain_percent(seats, 0.0);
  for (std::size_t seat = 0; seat < seats; ++seat) {
    gain_percent[seat] = 100.0 * gain[seat] / initial_pot;
  }
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
  const double seconds = seconds_since(started);
  const auto &rake = config.value().rake;
  std::ofstream summary(output_dir / "summary.json", std::ios::binary);
  summary << std::setprecision(12) << "{\n"
          << "  \"schema\": \"gtosd.preflop_blueprint_checkdown_classes.v1\",\n"
          << "  \"config_id\": \"" << config.value().id << "\",\n"
          << "  \"tree_fingerprint\": \"" << game.fingerprint() << "\",\n"
          << "  \"seats\": " << seats << ",\n"
          << "  \"terminal_source\": {\"file\": \"" << terminal_source << "\", \"fingerprint\": \""
          << terminal_fingerprint << "\"},\n"
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
          << "  \"seconds_per_iteration\": " << iteration_seconds / static_cast<double>(iterations)
          << ",\n"
          << "  \"charts\": " << written.size() << ",\n";
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
  summary << "  \"trajectory\": [\n" << trajectory.str() << "\n  ]\n}\n";
  summary.close();
  if (!summary) {
    throw std::runtime_error("cannot write " + (output_dir / "summary.json").string());
  }
  std::cout << std::setprecision(12) << "EV [" << join(ev) << "] antes, expected rake -(sum EV) "
            << -ev_sum << ", by reach " << direct_rake << " antes\n"
            << "gain [" << join(gain) << "] antes, [" << join(gain_percent) << "] % of the pot; "
            << iteration_seconds / static_cast<double>(iterations) << " s per iteration\n";
  if (compared) {
    std::cout << "equivalence: " << equivalence.files << " charts, " << equivalence.rows
              << " rows, largest chart difference " << equivalence.chart << " ("
              << equivalence.cells_differing << " of " << equivalence.cells
              << " cells differ; worst " << equivalence.chart_worst << "), EV " << equivalence.ev
              << ", gain " << equivalence.gain << ", expected rake " << equivalence.rake
              << " antes\n";
    if (!equivalent) {
      throw std::runtime_error("not equivalent to the reference within chart tolerance " +
                               std::to_string(chart_tolerance) + " and EV tolerance " +
                               std::to_string(ev_tolerance) + " antes");
    }
  }
  std::cout << "PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES=PASS seats=" << seats
            << " charts=" << written.size() << " max_gain_pot_percent=" << largest(gain_percent)
            << (compared ? " equivalence=PASS" : "") << '\n';
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
