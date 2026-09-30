// Compiles the public game tree of a preflop blueprint configuration and
// reports its counts, the regret/strategy state size for two bucket capacity
// sets and the compile time. With --actions it also lists every preflop
// decision node with its legal actions (labels and amounts) and the action
// counts of the postflop decision nodes per street: the record of which sizes
// the engine kept, merged with the all-in or dropped for a given stack.
// With --dump-nodes <file> it also writes every node of the compiled tree as
// JSON Lines (one header record, one record per node in preorder, one end
// record): kind, street, actor, the path from the root as (action, amount)
// edges, the public state (pot, commitments, initial contributions, stacks,
// uncalled returns, status), the outgoing edges and, at every terminal, the
// stored payoff row of every winner subset. Everything is read through the
// public CompiledGame API, so the dump is the tree the trainer and the
// evaluators consume; tools/independent/sd_referee.py checks it against the
// rules.

#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

std::string json_string(const std::string &value) {
  std::string out = "\"";
  for (const auto character : value) {
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    out.push_back(character);
  }
  out.push_back('"');
  return out;
}

void print_actions(const pb::CompiledGame &game) {
  std::cout << "  \"preflop_decisions\": [";
  bool first = true;
  for (const auto node : pb::preflop_decision_nodes(game)) {
    const auto &entry = game.nodes()[node];
    const auto labels = pb::edge_labels(game, node);
    const auto edges = game.edges_of(node);
    std::cout << (first ? "\n" : ",\n") << "    {\"node\": " << node << ", \"id\": "
              << json_string(pb::node_path_id(game, node)) << ", \"actor\": "
              << json_string(pb::position_name(game, entry.actor)) << ", \"level\": "
              << static_cast<unsigned>(entry.level) << ", \"limped_pot\": "
              << (entry.limped_pot ? "true" : "false") << ", \"actions\": [";
    for (std::size_t action = 0; action < labels.size(); ++action) {
      std::cout << (action == 0 ? "" : ", ") << "{\"label\": " << json_string(labels[action])
                << ", \"amount_units\": " << edges[action].action.amount.units()
                << ", \"all_in\": "
                << (edges[action].action.type == gtosd::ActionType::AllIn ? "true" : "false")
                << "}";
    }
    std::cout << "]}";
    first = false;
  }
  std::cout << "\n  ],\n";
  // Postflop: histogram of (street, actor, action labels) over the decision
  // nodes so a reader sees which sizes survive at every street for this stack.
  std::map<std::string, std::uint32_t> signatures;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision || node.street == gtosd::Street::Preflop) {
      continue;
    }
    std::string signature = std::string(pb::street_name(node.street)) + "|";
    for (const auto &label : pb::edge_labels(game, node.id)) {
      signature += label + ";";
    }
    ++signatures[signature];
  }
  std::cout << "  \"postflop_action_signatures\": {";
  first = true;
  for (const auto &[signature, count] : signatures) {
    std::cout << (first ? "\n" : ",\n") << "    " << json_string(signature) << ": " << count;
    first = false;
  }
  std::cout << "\n  },\n";
}

const char *action_type_name(const gtosd::ActionType type) noexcept {
  switch (type) {
  case gtosd::ActionType::Fold:
    return "fold";
  case gtosd::ActionType::Check:
    return "check";
  case gtosd::ActionType::Call:
    return "call";
  case gtosd::ActionType::Bet:
    return "bet";
  case gtosd::ActionType::Raise:
    return "raise";
  case gtosd::ActionType::AllIn:
    return "all_in";
  }
  return "unknown";
}

const char *all_in_kind_name(const gtosd::AllInKind kind) noexcept {
  switch (kind) {
  case gtosd::AllInKind::None:
    return "none";
  case gtosd::AllInKind::Call:
    return "call";
  case gtosd::AllInKind::Raise:
    return "raise";
  }
  return "unknown";
}

const char *status_name(const gtosd::HandStatus status) noexcept {
  switch (status) {
  case gtosd::HandStatus::InProgress:
    return "in_progress";
  case gtosd::HandStatus::StreetComplete:
    return "street_complete";
  case gtosd::HandStatus::Folded:
    return "folded";
  case gtosd::HandStatus::AllInRunout:
    return "all_in_runout";
  case gtosd::HandStatus::Showdown:
    return "showdown";
  }
  return "unknown";
}

// Path token of the edge `edge` of node `node`: "<type>:<amount units>" for an
// action, "chance" for the single edge of a street transition.
std::string edge_token(const pb::CompiledGame &game, const std::uint32_t node,
                       const std::size_t edge) {
  if (game.nodes()[node].kind == pb::NodeKind::Chance) {
    return "\"chance\"";
  }
  const auto &action = game.edges_of(node)[edge].action;
  return "\"" + std::string(action_type_name(action.type)) + ":" +
         std::to_string(action.amount.units()) + "\"";
}

template <typename Values>
void write_units(std::ostream &output, const Values &values, const std::uint8_t players) {
  output << '[';
  for (std::uint8_t player = 0; player < players; ++player) {
    output << (player == 0U ? "" : ",") << values[player].units();
  }
  output << ']';
}

void write_payoff_row(std::ostream &output, const std::uint32_t winners,
                      const std::span<const std::int64_t> row) {
  output << "{\"winners\":" << winners << ",\"payoff\":[";
  for (std::size_t player = 0; player < row.size(); ++player) {
    output << (player == 0U ? "" : ",") << row[player];
  }
  output << "]}";
}

// JSON Lines dump of the whole compiled tree (schema
// gtosd.preflop_blueprint_game_nodes.v1). Amounts are money units (10,000 per
// ante); an action amount is the chips the actor adds with it.
std::uint64_t dump_nodes(const pb::CompiledGame &game, const std::string_view mode,
                         const std::filesystem::path &path) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot write " + path.string());
  }
  const auto &config = game.config();
  const auto &nodes = game.nodes();
  const auto &states = game.states();
  const auto players = config.player_count;
  output << "{\"record\":\"header\",\"schema\":\"gtosd.preflop_blueprint_game_nodes.v1\""
         << ",\"config_id\":" << json_string(config.id)
         << ",\"config_fingerprint\":" << json_string(pb::game_config_fingerprint(config))
         << ",\"tree_fingerprint\":" << json_string(game.fingerprint()) << ",\"mode\":\"" << mode
         << "\",\"player_count\":" << static_cast<unsigned>(players) << ",\"positions\":[";
  for (std::size_t seat = 0; seat < config.positions.size(); ++seat) {
    output << (seat == 0U ? "" : ",") << json_string(config.positions[seat]);
  }
  output << "],\"units_per_ante\":" << gtosd::Money::units_per_ante
         << ",\"node_count\":" << nodes.size() << ",\"edge_count\":" << game.edges().size()
         << "}\n";

  // Incoming edge of every node (index at the parent and path token).
  std::vector<std::uint32_t> incoming_index(nodes.size(), 0U);
  std::vector<std::string> incoming_token(nodes.size());
  for (const auto &node : nodes) {
    const auto edges = game.edges_of(node.id);
    for (std::size_t edge = 0; edge < edges.size(); ++edge) {
      incoming_index[edges[edge].child] = static_cast<std::uint32_t>(edge);
      incoming_token[edges[edge].child] = edge_token(game, node.id, edge);
    }
  }

  // Preorder: when a node is visited, tokens[0, depth - 1) already hold the
  // edges of its ancestors.
  std::vector<std::string> tokens;
  std::uint64_t records = 0U;
  for (const auto &node : nodes) {
    const auto &state = states[node.id];
    tokens.resize(node.depth);
    if (node.depth > 0U) {
      tokens[node.depth - 1U] = incoming_token[node.id];
    }
    output << "{\"record\":\"node\",\"id\":" << node.id << ",\"parent\":";
    if (node.parent == pb::no_node) {
      output << "null,\"edge_index\":null";
    } else {
      output << node.parent << ",\"edge_index\":" << incoming_index[node.id];
    }
    output << ",\"depth\":" << node.depth << ",\"kind\":\"" << pb::node_kind_name(node.kind)
           << "\",\"street\":\"" << pb::street_name(node.street) << "\",\"actor\":";
    if (node.actor == pb::no_player) {
      output << "null";
    } else {
      output << static_cast<unsigned>(node.actor);
    }
    output << ",\"level\":" << static_cast<unsigned>(node.level)
           << ",\"limped_pot\":" << (node.limped_pot ? "true" : "false") << ",\"postflop_entry\":";
    if (node.postflop_entry == pb::no_entry) {
      output << "null";
    } else {
      output << node.postflop_entry;
    }
    output << ",\"remaining_board_cards\":" << static_cast<unsigned>(node.remaining_board_cards)
           << ",\"active_mask\":" << static_cast<unsigned>(node.active_mask) << ",\"status\":\""
           << status_name(state.status) << "\",\"state_street\":\"" << pb::street_name(state.street)
           << "\",\"player_to_act\":" << static_cast<unsigned>(state.player_to_act)
           << ",\"all_in_mask\":" << static_cast<unsigned>(state.all_in_players_mask)
           << ",\"terminal_winner_mask\":" << static_cast<unsigned>(state.terminal_winner_mask)
           << ",\"pot\":" << state.pot.units() << ",\"initial_pot\":" << state.initial_pot.units()
           << ",\"current_bet\":" << state.current_bet.units()
           << ",\"last_full_raise_increment\":" << state.last_full_raise_increment.units()
           << ",\"returned_uncalled\":" << state.returned_uncalled.units()
           << ",\"initial_pot_contributions\":";
    write_units(output, state.initial_pot_contributions, players);
    output << ",\"committed_total\":";
    write_units(output, state.committed_total, players);
    output << ",\"committed_this_street\":";
    write_units(output, state.committed_this_street, players);
    output << ",\"remaining_stacks\":";
    write_units(output, state.remaining_stacks, players);
    output << ",\"returned_uncalled_by_player\":";
    write_units(output, state.returned_uncalled_by_player, players);
    output << ",\"path\":[";
    for (std::size_t step = 0; step < tokens.size(); ++step) {
      output << (step == 0U ? "" : ",") << tokens[step];
    }
    output << "],\"edges\":[";
    const auto edges = game.edges_of(node.id);
    for (std::size_t edge = 0; edge < edges.size(); ++edge) {
      output << (edge == 0U ? "" : ",");
      if (node.kind == pb::NodeKind::Chance) {
        output << "{\"type\":\"chance\",\"child\":" << edges[edge].child << "}";
        continue;
      }
      const auto &action = edges[edge].action;
      output << "{\"type\":\"" << action_type_name(action.type)
             << "\",\"amount\":" << action.amount.units() << ",\"all_in_kind\":\""
             << all_in_kind_name(action.all_in_kind)
             << "\",\"requested_basis_points\":" << action.requested_basis_points
             << ",\"child\":" << edges[edge].child << "}";
    }
    output << "],\"payoffs\":[";
    if (node.kind == pb::NodeKind::TerminalFold) {
      write_payoff_row(output, state.terminal_winner_mask, game.fold_payoffs(node.id));
    } else if (node.kind == pb::NodeKind::TerminalShowdown) {
      bool first_row = true;
      const std::uint32_t active = node.active_mask;
      for (std::uint32_t winners = 1U; winners <= active; ++winners) {
        if ((winners & ~active) != 0U) {
          continue;
        }
        output << (first_row ? "" : ",");
        write_payoff_row(output, winners,
                         game.showdown_payoffs(node.id, static_cast<std::uint8_t>(winners)));
        first_row = false;
      }
    }
    output << "]}\n";
    ++records;
  }
  output << "{\"record\":\"end\",\"nodes\":" << records << "}\n";
  output.flush();
  if (!output) {
    throw std::runtime_error("write failed: " + path.string());
  }
  return records;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path config_path;
    std::uint32_t flop = 200U;
    std::uint32_t turn = 500U;
    std::uint32_t river = 1'000U;
    std::uint32_t alt_flop = 500U;
    std::uint32_t alt_turn = 1'000U;
    std::uint32_t alt_river = 2'000U;
    bool actions = false;
    std::filesystem::path dump_path;
    pb::CompileOptions options;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--preflop-only") {
        options.preflop_only = true;
        continue;
      }
      // The step-1 (checkdown) tree: every postflop entry settles as a showdown.
      if (name == "--checkdown") {
        options.checkdown_at_flop = true;
        continue;
      }
      if (name == "--actions") {
        actions = true;
        continue;
      }
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{name});
      }
      const std::string_view value = argv[++index];
      if (name == "--config") {
        config_path = value;
      } else if (name == "--flop") {
        flop = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--turn") {
        turn = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--river") {
        river = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--alt-flop") {
        alt_flop = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--alt-turn") {
        alt_turn = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--alt-river") {
        alt_river = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--max-nodes") {
        options.maximum_nodes = parse_unsigned(value);
      } else if (name == "--dump-nodes") {
        dump_path = value;
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
              << (options.checkdown_at_flop ? "  \"checkdown\": true,\n" : "")
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
    if (actions) {
      print_actions(game);
    }
    if (!dump_path.empty()) {
      const auto mode = options.checkdown_at_flop ? "checkdown"
                        : options.preflop_only    ? "preflop_only"
                                                  : "full";
      const auto records = dump_nodes(game, mode, dump_path);
      std::cout << "  \"dump_nodes\": " << json_string(dump_path.string())
                << ", \"dump_records\": " << records << ",\n";
    }
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
