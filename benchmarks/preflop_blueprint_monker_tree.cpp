// Checks that a preflop blueprint configuration builds MonkerSolver's preflop
// tree: the files of a MonkerSolver chart set (one per preflop decision node,
// named after the line that reaches it), or a manifest of them, against the
// preflop decision nodes of the compiled game as chart_nodes names them.
//   C1 node set: missing and extra files (chart_nodes throws on two nodes
//      sharing a name).
//   C2 actions: the header columns of every common file against the node's
//      edges in the file's column order; a different set and a different
//      order are reported apart, tokens of an unknown form explicitly.
//   C3 (chart folder only) the 81 short-deck hand classes in every file and
//      every row total 0 or 1 within the three-decimal rounding.
//   C4 (pot mode only) every sized raise adds the configured fraction of the
//      pot after the call, within one unit, on top of the call.
//   C5 summary: decisions per position, action signatures, the preflop
//      statistics with the players of every postflop entry, and every node's
//      pot, amount to call, level, limp flag and live commitments.
// Every failure is listed; the tool does not stop at the first one. With both
// --charts and --manifest the manifest is also checked against the folder.
//
// A manifest (written by --write-manifest from a chart folder) holds one line
// per chart file, "path<TAB>column,column,..." in the file's column order,
// and '#' comments: the tree structure without frequencies, so a CTest does
// not need the chart folder. --write-manifest writes only what --manifest
// reads back: it refuses a folder with an unreadable chart (no columns to
// list) before creating the file, and checks the round trip after writing.
#include "monker_chart_format.hpp"

#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace pb = gtosd::preflop_blueprint;
namespace mc = gtosd::monker_charts;
using Json = nlohmann::ordered_json;

// Action columns of every chart file, by path relative to the chart folder.
using Columns = std::map<std::string, std::vector<std::string>>;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<std::string> split(const std::string &text, const char separator) {
  std::vector<std::string> fields;
  std::size_t start = 0U;
  while (true) {
    const auto found = text.find(separator, start);
    fields.push_back(
        text.substr(start, found == std::string::npos ? std::string::npos : found - start));
    if (found == std::string::npos) {
      return fields;
    }
    start = found + 1U;
  }
}

std::string joined(const std::vector<std::string> &tokens) {
  std::string text;
  for (const auto &token : tokens) {
    text += (text.empty() ? "" : ",") + token;
  }
  return text;
}

// Fold, Check, Call, AllIn or a street commitment such as 6.0ante.
bool known_token(const std::string &token) {
  if (token == "Fold" || token == "Check" || token == "Call" || token == "AllIn") {
    return true;
  }
  constexpr std::string_view suffix = "ante";
  if (token.size() <= suffix.size() || !token.ends_with(suffix)) {
    return false;
  }
  const auto amount = token.substr(0U, token.size() - suffix.size());
  std::size_t consumed = 0U;
  try {
    return std::stod(amount, &consumed) > 0.0 && consumed == amount.size();
  } catch (const std::exception &) {
    return false;
  }
}

// Signature of a node for the summary: its columns with every sized raise
// written "Size" (AllIn,Size,Call,Fold).
std::string signature(const std::vector<std::string> &tokens) {
  std::vector<std::string> generic;
  for (const auto &token : tokens) {
    const bool sized = token != "Fold" && token != "Check" && token != "Call" && token != "AllIn";
    generic.push_back(sized ? "Size" : token);
  }
  return joined(generic);
}

Columns read_manifest(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  Columns columns;
  std::string line;
  std::size_t number = 0U;
  while (std::getline(input, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto fields = split(line, '\t');
    if (fields.size() != 2U || fields[0].empty() || fields[1].empty()) {
      throw std::runtime_error(path.string() + ":" + std::to_string(number) +
                               ": expected <chart path><TAB><columns>");
    }
    if (!columns.emplace(fields[0], split(fields[1], ',')).second) {
      throw std::runtime_error(path.string() + ": " + fields[0] + " listed twice");
    }
  }
  return columns;
}

// The last two components of a chart folder ("3-way/50a"), for the manifest
// header.
std::string folder_title(const std::filesystem::path &directory) {
  std::vector<std::string> parts;
  for (const auto &part : directory.lexically_normal()) {
    if (!part.empty() && part != "." && part.generic_string() != "/") {
      parts.push_back(part.generic_string());
    }
  }
  if (parts.size() < 2U) {
    return parts.empty() ? std::string{} : parts.back();
  }
  return parts[parts.size() - 2U] + "/" + parts.back();
}

// Writes the manifest of a chart folder. Every line must read back through
// read_manifest: an unreadable chart has no columns, and a path or a column
// holding a separator would not split back, so such a folder is refused
// before the file is created; the written file is then read back and compared.
void write_manifest(const std::filesystem::path &path, const std::string &title,
                    const Columns &columns) {
  std::vector<std::string> problems;
  for (const auto &[file, tokens] : columns) {
    if (tokens.empty()) {
      problems.push_back(file + ": unreadable chart, no columns to list");
      continue;
    }
    if (file.empty() || file.front() == '#' || file.find_first_of("\t\r\n") != std::string::npos) {
      problems.push_back(file + ": the path cannot be a manifest key");
    }
    for (const auto &token : tokens) {
      if (token.empty() || token.find_first_of(",\t\r\n") != std::string::npos) {
        problems.push_back(file + ": the column '" + token + "' cannot be listed");
      }
    }
  }
  if (!problems.empty()) {
    std::string message = "--write-manifest refused: " + std::to_string(problems.size()) +
                          " chart file(s) that --manifest could not read back";
    for (const auto &problem : problems) {
      message += "\n  " + problem;
    }
    throw std::runtime_error(message);
  }
  {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
      throw std::runtime_error("cannot write " + path.string());
    }
    output << "# MonkerSolver preflop tree (" << title
           << "): chart path <TAB> action columns in file order (header without "
              "Combination/Total)\n";
    for (const auto &[file, tokens] : columns) {
      output << file << '\t' << joined(tokens) << '\n';
    }
    if (!output) {
      throw std::runtime_error("cannot write " + path.string());
    }
  }
  if (read_manifest(path) != columns) {
    throw std::runtime_error("the manifest " + path.string() +
                             " does not read back to the chart folder");
  }
}

struct ChartFolder {
  Columns columns;
  // C3 failures: unreadable files, hand classes, row totals.
  std::vector<std::string> class_failures;
};

ChartFolder read_chart_folder(const std::filesystem::path &directory) {
  if (!std::filesystem::is_directory(directory)) {
    throw std::runtime_error("no chart folder " + directory.string());
  }
  std::set<std::string> labels;
  for (const auto &entry : mc::hand_classes().combos_by_label) {
    labels.insert(entry.first);
  }
  ChartFolder folder;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file() || !entry.path().filename().string().ends_with("_strategy.txt")) {
      continue;
    }
    const auto relative = std::filesystem::relative(entry.path(), directory).generic_string();
    mc::ChartFile chart;
    try {
      chart = mc::read_chart(entry.path());
    } catch (const std::exception &error) {
      // No columns: the file counts for C1, C2 skips it.
      folder.columns[relative] = {};
      folder.class_failures.push_back(relative + ": " + error.what());
      continue;
    }
    folder.columns[relative] = chart.columns;
    std::set<std::string> rows;
    // Every column is rounded to three decimals.
    const double tolerance = 5e-4 * static_cast<double>(chart.columns.size()) + 1e-9;
    for (const auto &[label, values] : chart.rows) {
      rows.insert(label);
      double total = 0.0;
      for (const auto value : values) {
        total += value;
      }
      if (std::abs(total) > tolerance && std::abs(total - 1.0) > tolerance) {
        folder.class_failures.push_back(relative + ": the row of " + label + " totals " +
                                        std::to_string(total));
      }
    }
    if (rows != labels) {
      folder.class_failures.push_back(relative + ": " + std::to_string(rows.size()) +
                                      " hand classes, not the " + std::to_string(labels.size()) +
                                      " short-deck classes");
    }
  }
  return folder;
}

Json units_list(const std::array<gtosd::Money, gtosd::maximum_players> &values,
                const std::uint8_t players) {
  Json list = Json::array();
  for (std::uint8_t player = 0; player < players; ++player) {
    list.push_back(values[player].units());
  }
  return list;
}

void report_list(const std::string_view check, const std::vector<std::string> &failures) {
  for (const auto &failure : failures) {
    std::cout << check << ": " << failure << '\n';
  }
}

int run(const int argc, char **argv) {
  std::filesystem::path config_path;
  std::filesystem::path charts_dir;
  std::filesystem::path manifest_path;
  std::filesystem::path write_manifest_path;
  std::filesystem::path json_path;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name(argv[index]);
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(name));
    }
    const std::string_view value(argv[++index]);
    if (name == "--config") {
      config_path = value;
    } else if (name == "--charts") {
      charts_dir = value;
    } else if (name == "--manifest") {
      manifest_path = value;
    } else if (name == "--write-manifest") {
      write_manifest_path = value;
    } else if (name == "--json") {
      json_path = value;
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (config_path.empty() || (charts_dir.empty() && manifest_path.empty())) {
    throw std::runtime_error("--config and --charts <folder> or --manifest <tsv> are required");
  }
  if (!write_manifest_path.empty() && charts_dir.empty()) {
    throw std::runtime_error("--write-manifest needs --charts");
  }
  const auto parsed = pb::parse_game_config_json(read_file(config_path));
  if (!parsed) {
    throw std::runtime_error(std::string("configuration rejected: ") +
                             pb::config_error_name(parsed.error()));
  }
  const auto &config = parsed.value();
  pb::CompileOptions options;
  options.preflop_only = true;
  const auto compiled = pb::CompiledGame::compile(config, options);
  if (!compiled) {
    throw std::runtime_error(std::string("compile failed: ") +
                             pb::game_model_error_name(compiled.error()));
  }
  const auto &game = compiled.value();
  const auto nodes = mc::chart_nodes(game);
  Columns expected;
  for (const auto &chart : nodes) {
    auto &tokens = expected[chart.relative];
    for (const auto index : chart.columns) {
      tokens.push_back(chart.tokens[index]);
    }
  }

  std::optional<ChartFolder> folder;
  if (!charts_dir.empty()) {
    folder = read_chart_folder(charts_dir);
  }
  std::optional<Columns> manifest;
  if (!manifest_path.empty()) {
    manifest = read_manifest(manifest_path);
  }
  const Columns &observed = folder ? folder->columns : manifest.value();
  if (!write_manifest_path.empty()) {
    write_manifest(write_manifest_path, folder_title(charts_dir), folder->columns);
  }

  // The manifest against the folder, when both are given.
  std::vector<std::string> manifest_failures;
  if (folder && manifest) {
    for (const auto &[file, tokens] : folder->columns) {
      const auto found = manifest->find(file);
      if (found == manifest->end()) {
        manifest_failures.push_back(file + " is not in the manifest");
      } else if (found->second != tokens) {
        manifest_failures.push_back(file + ": manifest " + joined(found->second) + ", file " +
                                    joined(tokens));
      }
    }
    for (const auto &[file, tokens] : manifest.value()) {
      if (!folder->columns.contains(file)) {
        manifest_failures.push_back(file + " is in the manifest but not in the folder");
      }
    }
  }

  // C1 and C2.
  std::vector<std::string> missing;
  std::vector<std::string> extra;
  std::vector<std::string> action_failures;
  std::vector<std::string> unknown_tokens;
  std::uint64_t order_failures = 0U;
  for (const auto &[file, tokens] : expected) {
    const auto found = observed.find(file);
    if (found == observed.end()) {
      missing.push_back(file);
      continue;
    }
    // A file without columns could not be read; C3 lists it.
    if (found->second == tokens || found->second.empty()) {
      continue;
    }
    auto theirs = found->second;
    auto ours = tokens;
    std::ranges::sort(theirs);
    std::ranges::sort(ours);
    const bool same_set = theirs == ours;
    order_failures += same_set ? 1U : 0U;
    action_failures.push_back(file + ": chart " + joined(found->second) + ", game " +
                              joined(tokens) + (same_set ? " (same actions, other order)" : ""));
  }
  for (const auto &[file, tokens] : observed) {
    if (!expected.contains(file)) {
      extra.push_back(file);
    }
    for (const auto &token : tokens) {
      if (!known_token(token)) {
        unknown_tokens.push_back(file + ": " + token);
      }
    }
  }

  // C4: the pot rule of every sized raise, recomputed from the public state
  // (half up, like the core; one unit of slack).
  std::vector<std::string> pot_rule_failures;
  std::uint64_t sized_raises = 0U;
  const bool pot_mode = !config.open_sizes.empty();
  if (pot_mode) {
    for (const auto &chart : nodes) {
      const auto &node = game.nodes()[chart.node];
      const auto &state = game.states()[chart.node];
      const auto to_call = gtosd::amount_to_call(state, node.actor).units();
      const auto pot_after_call = state.pot.units() + to_call;
      const auto edges = game.edges_of(chart.node);
      for (std::size_t index = 0; index < edges.size(); ++index) {
        const auto &action = edges[index].action;
        if (action.type != gtosd::ActionType::Bet && action.type != gtosd::ActionType::Raise) {
          continue;
        }
        ++sized_raises;
        const auto added = action.amount.units() - to_call;
        const bool sized = std::ranges::any_of(config.open_sizes, [&](const auto size) {
          const auto increment =
              (pot_after_call * static_cast<std::int64_t>(size.basis_points()) + 5'000) / 10'000;
          return std::abs(added - increment) <= 1;
        });
        const bool committed =
            game.states()[edges[index].child].committed_this_street[node.actor].units() ==
            state.committed_this_street[node.actor].units() + action.amount.units();
        if (!sized || !committed) {
          pot_rule_failures.push_back(chart.relative + ": " + chart.tokens[index] + " adds " +
                                      std::to_string(added) + " units above a call of " +
                                      std::to_string(to_call) + " into a pot of " +
                                      std::to_string(pot_after_call) + " after the call");
        }
      }
    }
  }

  // C5: summary.
  const auto &stats = game.stats();
  std::map<std::string, std::uint64_t> decisions_by_position;
  std::map<std::string, std::uint64_t> signatures;
  Json node_list = Json::array();
  for (const auto &chart : nodes) {
    const auto &node = game.nodes()[chart.node];
    const auto &state = game.states()[chart.node];
    const auto &tokens = expected.at(chart.relative);
    ++decisions_by_position[chart.position];
    ++signatures[signature(tokens)];
    const auto found = observed.find(chart.relative);
    Json entry;
    entry["file"] = chart.relative;
    entry["node"] = chart.node;
    entry["actor"] = chart.position;
    entry["pot_units"] = state.pot.units();
    entry["to_call_units"] = gtosd::amount_to_call(state, node.actor).units();
    entry["level"] = static_cast<unsigned>(node.level);
    entry["limped_pot"] = node.limped_pot;
    entry["live_units"] = units_list(state.committed_this_street, state.player_count);
    entry["actions"] = tokens;
    entry["status"] = found == observed.end()   ? "missing"
                      : found->second == tokens ? "ok"
                      : found->second.empty()   ? "unreadable"
                                                : "actions_differ";
    node_list.push_back(std::move(entry));
  }
  std::map<unsigned, std::uint64_t> entries_by_players;
  for (const auto entry : game.postflop_entries()) {
    ++entries_by_players[static_cast<unsigned>(std::popcount(game.nodes()[entry].active_mask))];
  }

  const auto failures = missing.size() + extra.size() + action_failures.size() +
                        unknown_tokens.size() + pot_rule_failures.size() +
                        manifest_failures.size() +
                        (folder ? folder->class_failures.size() : std::size_t{0});
  const bool pass = failures == 0U;

  report_list("missing", missing);
  report_list("extra", extra);
  report_list("actions", action_failures);
  report_list("unknown_token", unknown_tokens);
  if (folder) {
    report_list("classes", folder->class_failures);
  }
  report_list("pot_rule", pot_rule_failures);
  report_list("manifest", manifest_failures);
  std::cout << "config " << config.id << " players=" << static_cast<unsigned>(config.player_count)
            << (pot_mode ? " pot mode" : " target mode") << ", tree " << game.fingerprint() << '\n'
            << "preflop: nodes=" << stats.preflop_nodes << " decisions=" << stats.preflop_decisions
            << " folds=" << stats.preflop_terminal_folds
            << " all_in_runouts=" << stats.preflop_all_in_runouts
            << " entries=" << stats.postflop_entries << " (by players:";
  for (const auto &[players, count] : entries_by_players) {
    std::cout << ' ' << players << "->" << count;
  }
  std::cout << ")\ndecisions by position:";
  for (const auto &[position, count] : decisions_by_position) {
    std::cout << ' ' << position << '=' << count;
  }
  std::cout << "\nsignatures:";
  for (const auto &[text, count] : signatures) {
    std::cout << ' ' << text << '=' << count;
  }
  std::cout << "\nchecked: files=" << observed.size() << " nodes=" << nodes.size()
            << " missing=" << missing.size() << " extra=" << extra.size()
            << " actions=" << action_failures.size() << " (order only " << order_failures << ")"
            << " unknown_tokens=" << unknown_tokens.size() << " classes="
            << (folder ? std::to_string(folder->class_failures.size()) : std::string("n/a"))
            << " pot_rule=" << (pot_mode ? std::to_string(pot_rule_failures.size()) : "n/a")
            << " sized_raises=" << sized_raises << " manifest="
            << (folder && manifest ? std::to_string(manifest_failures.size()) : "n/a") << '\n';

  if (!json_path.empty()) {
    Json report;
    report["schema"] = "gtosd.preflop_blueprint_monker_tree_report.v1";
    report["config_id"] = config.id;
    report["config_fingerprint"] = pb::game_config_fingerprint(config);
    report["tree_fingerprint"] = game.fingerprint();
    report["player_count"] = static_cast<unsigned>(config.player_count);
    report["pot_mode"] = pot_mode;
    report["charts"] = charts_dir.empty() ? Json(nullptr) : Json(charts_dir.generic_string());
    report["manifest"] =
        manifest_path.empty() ? Json(nullptr) : Json(manifest_path.generic_string());
    report["result"] = pass ? "PASS" : "FAIL";
    report["counts"] = {
        {"nodes", nodes.size()},
        {"files", observed.size()},
        {"missing", missing.size()},
        {"extra", extra.size()},
        {"action_mismatches", action_failures.size()},
        {"action_order_mismatches", order_failures},
        {"unknown_tokens", unknown_tokens.size()},
        {"class_failures", folder ? Json(folder->class_failures.size()) : Json(nullptr)},
        {"pot_rule_failures", pot_mode ? Json(pot_rule_failures.size()) : Json(nullptr)},
        {"sized_raises", sized_raises},
        {"manifest_mismatches",
         folder && manifest ? Json(manifest_failures.size()) : Json(nullptr)}};
    report["missing"] = missing;
    report["extra"] = extra;
    report["action_mismatches"] = action_failures;
    report["unknown_tokens"] = unknown_tokens;
    report["class_failures"] = folder ? Json(folder->class_failures) : Json::array();
    report["pot_rule_failures"] = pot_rule_failures;
    report["manifest_mismatches"] = manifest_failures;
    report["preflop"] = {{"nodes", stats.preflop_nodes},
                         {"decisions", stats.preflop_decisions},
                         {"terminal_folds", stats.preflop_terminal_folds},
                         {"all_in_runouts", stats.preflop_all_in_runouts},
                         {"postflop_entries", stats.postflop_entries}};
    Json by_players = Json::object();
    for (const auto &[players, count] : entries_by_players) {
      by_players[std::to_string(players)] = count;
    }
    report["postflop_entries_by_players"] = std::move(by_players);
    report["decisions_by_position"] = decisions_by_position;
    report["signatures"] = signatures;
    report["nodes"] = std::move(node_list);
    std::ofstream output(json_path, std::ios::binary);
    output << report.dump(2) << '\n';
    if (!output) {
      throw std::runtime_error("cannot write " + json_path.string());
    }
  }

  if (pass) {
    std::cout << "PREFLOP_BLUEPRINT_MONKER_TREE=PASS nodes=" << nodes.size()
              << " files=" << observed.size() << '\n';
    return 0;
  }
  std::cout << "PREFLOP_BLUEPRINT_MONKER_TREE=FAIL missing=" << missing.size()
            << " extra=" << extra.size() << " actions=" << action_failures.size()
            << " unknown_tokens=" << unknown_tokens.size()
            << " classes=" << (folder ? folder->class_failures.size() : std::size_t{0})
            << " pot_rule=" << pot_rule_failures.size() << " manifest=" << manifest_failures.size()
            << '\n';
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_MONKER_TREE=FAIL " << error.what() << '\n';
    return 1;
  }
}
