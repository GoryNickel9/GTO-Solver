// Compiles the public game tree of a preflop blueprint configuration and
// reports its counts, the regret/strategy state size for two bucket capacity
// sets and the compile time.

#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace pb = gtosd::preflop_blueprint;

std::uint64_t parse_unsigned(const std::string_view value) {
  std::size_t consumed = 0U;
  const auto parsed = std::stoull(std::string{value}, &consumed, 10);
  if (consumed != value.size()) {
    throw std::runtime_error("invalid number: " + std::string{value});
  }
  return parsed;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void print_layout(const std::string_view name, const pb::StateLayout &layout, const bool last) {
  std::cout << "  \"" << name << "\": {\"flop_capacity\": " << layout.flop_capacity
            << ", \"turn_capacity\": " << layout.turn_capacity
            << ", \"river_capacity\": " << layout.river_capacity
            << ", \"entries\": " << layout.entries
            << ", \"entries_preflop\": " << layout.entries_by_street[0]
            << ", \"entries_flop\": " << layout.entries_by_street[1]
            << ", \"entries_turn\": " << layout.entries_by_street[2]
            << ", \"entries_river\": " << layout.entries_by_street[3]
            << ", \"table_bytes\": " << layout.table_bytes()
            << ", \"state_bytes\": " << layout.state_bytes() << "}" << (last ? "\n" : ",\n");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path config_path;
    std::uint16_t flop = 200U;
    std::uint16_t turn = 500U;
    std::uint16_t river = 1'000U;
    std::uint16_t alt_flop = 500U;
    std::uint16_t alt_turn = 1'000U;
    std::uint16_t alt_river = 2'000U;
    pb::CompileOptions options;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--preflop-only") {
        options.preflop_only = true;
        continue;
      }
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{name});
      }
      const std::string_view value = argv[++index];
      if (name == "--config") {
        config_path = value;
      } else if (name == "--flop") {
        flop = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--turn") {
        turn = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--river") {
        river = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--alt-flop") {
        alt_flop = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--alt-turn") {
        alt_turn = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--alt-river") {
        alt_river = static_cast<std::uint16_t>(parse_unsigned(value));
      } else if (name == "--max-nodes") {
        options.maximum_nodes = parse_unsigned(value);
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    if (config_path.empty()) {
      throw std::runtime_error("--config <game fixture> is required");
    }
    const auto config = pb::parse_game_config_json(read_file(config_path));
    if (!config) {
      throw std::runtime_error(std::string("configuration rejected: ") +
                               pb::config_error_name(config.error()));
    }
    const auto compiled = pb::CompiledGame::compile(config.value(), options);
    if (!compiled) {
      throw std::runtime_error(std::string("compile failed: ") +
                               pb::game_model_error_name(compiled.error()));
    }
    const auto &game = compiled.value();
    const auto &stats = game.stats();
    const auto baseline = pb::layout_state(game, flop, turn, river);
    const auto alternative = pb::layout_state(game, alt_flop, alt_turn, alt_river);

    std::cout << "{\n"
              << "  \"schema\": \"gtosd.preflop_blueprint_game_report.v1\",\n"
              << "  \"config_id\": \"" << config.value().id << "\",\n"
              << "  \"config_fingerprint\": \"" << pb::game_config_fingerprint(config.value())
              << "\",\n"
              << "  \"player_count\": " << static_cast<unsigned>(config.value().player_count)
              << ",\n"
              << "  \"preflop_only\": " << (options.preflop_only ? "true" : "false") << ",\n"
              << "  \"tree_fingerprint\": \"" << game.fingerprint() << "\",\n"
              << "  \"node_count\": " << stats.node_count << ", \"edge_count\": " << stats.edge_count
              << ", \"decision_nodes\": " << stats.decision_nodes
              << ", \"chance_nodes\": " << stats.chance_nodes
              << ", \"terminal_folds\": " << stats.terminal_folds
              << ", \"terminal_showdowns\": " << stats.terminal_showdowns << ",\n"
              << "  \"preflop\": {\"nodes\": " << stats.preflop_nodes
              << ", \"decisions\": " << stats.preflop_decisions
              << ", \"terminal_folds\": " << stats.preflop_terminal_folds
              << ", \"all_in_runouts\": " << stats.preflop_all_in_runouts
              << ", \"postflop_entries\": " << stats.postflop_entries << "},\n"
              << "  \"postflop\": {\"represented_nodes\": " << stats.postflop_represented_nodes
              << ", \"action_edges\": " << stats.postflop_action_edges
              << ", \"decisions\": " << stats.postflop_decisions
              << ", \"decisions_flop\": " << stats.postflop_decisions_by_street[0]
              << ", \"decisions_turn\": " << stats.postflop_decisions_by_street[1]
              << ", \"decisions_river\": " << stats.postflop_decisions_by_street[2]
              << ", \"chance_frontiers\": " << stats.postflop_chance_frontiers
              << ", \"terminal_folds\": " << stats.postflop_terminal_folds
              << ", \"terminal_showdowns\": " << stats.postflop_terminal_showdowns
              << ", \"terminal_all_in_runouts\": " << stats.postflop_terminal_all_in_runouts
              << "},\n"
              << "  \"maximum_depth\": " << stats.maximum_depth
              << ", \"maximum_raise_count\": " << static_cast<unsigned>(stats.maximum_raise_count)
              << ",\n"
              << "  \"states_bytes\": " << game.states().size() * sizeof(gtosd::PublicState)
              << ", \"nodes_bytes\": " << game.nodes().size() * sizeof(pb::CompiledNode)
              << ", \"edges_bytes\": " << game.edges().size() * sizeof(pb::CompiledEdge) << ",\n";
    print_layout("layout_baseline", baseline, false);
    print_layout("layout_alternative", alternative, false);
    std::cout << "  \"compile_seconds\": " << stats.compile_seconds << "\n}\n";
    std::cout << "PREFLOP_BLUEPRINT_GAME=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_GAME=FAIL " << error.what() << '\n';
    return 1;
  }
}
