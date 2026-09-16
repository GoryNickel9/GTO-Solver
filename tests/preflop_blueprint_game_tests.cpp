#include "gtosd/core/game.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"

#include <bit>
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

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

pb::GameConfig load_fixture(const std::string_view name) {
  const auto path =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "fixtures" / std::string(name);
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), std::string("fixture parses: ") + std::string(name));
  return parsed.value();
}

gtosd::Money antes(const std::int64_t value) {
  return gtosd::Money::from_antes(value).value();
}

gtosd::Money units(const std::int64_t value) { return gtosd::Money::from_units(value).value(); }

// Fingerprint format of the archived HU preflop tree ("gtosd.hu_preflop_tree.v2").
// The CO40 benchmark froze the value fnv1a64:a68337fa567aa2d9 for it; the
// compiled preflop part must reproduce it node by node and edge by edge.
constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

std::uint64_t fnv1a(const std::string_view value, std::uint64_t hash = fnv_offset) {
  for (const auto character : value) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= fnv_prime;
  }
  return hash;
}

std::string hex64(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16U, '0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[result.size() - 1U - index] = digits[static_cast<std::size_t>(value & 0xFU)];
    value >>= 4U;
  }
  return result;
}

std::string legacy_preflop_fingerprint(const pb::CompiledGame &game) {
  const auto &rake = game.config().rake;
  auto hash = fnv1a("gtosd.hu_preflop_tree.v2");
  hash = fnv1a(std::to_string(rake.enabled) + ":" + std::to_string(rake.percentage.basis_points()) +
                   ":" + std::to_string(rake.cap.units()) + ":" +
                   std::to_string(rake.no_flop_no_drop) + ":" +
                   std::to_string(rake.minimum_pot.units()) + ";",
               hash);
  for (const auto &node : game.nodes()) {
    if (node.street != gtosd::Street::Preflop) {
      continue;
    }
    hash = fnv1a(gtosd::serialize_public_state(game.states()[node.id]), hash);
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    for (const auto &edge : game.edges_of(node.id)) {
      hash = fnv1a(std::to_string(static_cast<unsigned>(edge.action.type)) + ":" +
                       std::to_string(edge.action.amount.units()) + ";",
                   hash);
    }
  }
  return "fnv1a64:" + hex64(hash);
}

pb::CompiledGame compile(const pb::GameConfig &config, const pb::CompileOptions &options = {}) {
  const auto compiled = pb::CompiledGame::compile(config, options);
  require(compiled.has_value(),
          std::string("compile succeeds for ") + config.id +
              (compiled ? "" : std::string(": ") + pb::game_model_error_name(compiled.error())));
  return compiled.value();
}

bool has_edge(const pb::CompiledGame &game, const std::uint32_t node, const gtosd::ActionType type,
              const std::int64_t amount_units) {
  for (const auto &edge : game.edges_of(node)) {
    if (edge.action.type == type && edge.action.amount.units() == amount_units) {
      return true;
    }
  }
  return false;
}

void test_root_state_matches_core() {
  for (const std::string_view name : {"preflop_blueprint_co40_v1.json",
                                      "preflop_blueprint_hu10_full_v1.json"}) {
    const auto config = load_fixture(name);
    const auto root = pb::make_preflop_state(config);
    require(root.has_value(), "preflop state builds");
    const auto core_root = gtosd::make_hu_preflop_state(config.effective_stack, config.ante);
    require(core_root.has_value() && core_root.value() == root.value(),
            "two-player preflop state equals the core HU constructor");
  }
}

void test_co40_preflop_tree(const pb::CompiledGame &game) {
  const auto &stats = game.stats();
  require(stats.preflop_nodes == 58U, "CO40 preflop part has 58 nodes");
  require(stats.preflop_decisions == 20U, "CO40 preflop part has 20 decisions");
  require(stats.postflop_entries == 9U, "CO40 preflop part has 9 postflop entries");
  require(stats.preflop_terminal_folds + stats.preflop_all_in_runouts == 29U,
          "CO40 preflop part has 29 terminals");
  require(game.postflop_entries().size() == 9U, "entry list has 9 ids");
  require(legacy_preflop_fingerprint(game) == "fnv1a64:a68337fa567aa2d9",
          "CO40 preflop part reproduces the frozen legacy tree fingerprint");

  const auto root = game.root();
  const auto &root_node = game.nodes()[root];
  require(root_node.kind == pb::NodeKind::Decision && root_node.actor == 0U &&
              root_node.action_count == 5U && root_node.level == 0U,
          "root: CO decides among five actions");
  require(has_edge(game, root, gtosd::ActionType::Fold, 0) &&
              has_edge(game, root, gtosd::ActionType::Call, 10'000) &&
              has_edge(game, root, gtosd::ActionType::Raise, 60'000) &&
              has_edge(game, root, gtosd::ActionType::Raise, 100'000) &&
              has_edge(game, root, gtosd::ActionType::AllIn, 390'000),
          "root actions: fold, call 1a, raise to 6a, raise to 10a, all-in 39a");

  // BTN facing the raise to 6a: fold, call, raise to 10.5a (incomplete, configured), all-in.
  std::uint32_t after_small_open = pb::no_node;
  for (const auto &edge : game.edges_of(root)) {
    if (edge.action.type == gtosd::ActionType::Raise && edge.action.amount.units() == 60'000) {
      after_small_open = edge.child;
    }
  }
  require(after_small_open != pb::no_node, "raise to 6a child exists");
  const auto &small = game.nodes()[after_small_open];
  require(small.kind == pb::NodeKind::Decision && small.actor == 1U && small.level == 1U &&
              small.action_count == 4U,
          "BTN facing 6a: four actions at aggression level 1");
  require(has_edge(game, after_small_open, gtosd::ActionType::Raise, 95'000),
          "BTN facing 6a may raise to 10.5a (pays 9.5a over the 1a blind)");
  std::uint32_t after_reraise = pb::no_node;
  for (const auto &edge : game.edges_of(after_small_open)) {
    if (edge.action.type == gtosd::ActionType::Raise) {
      after_reraise = edge.child;
    }
  }
  const auto &final_response = game.nodes()[after_reraise];
  require(final_response.kind == pb::NodeKind::Decision && final_response.level == 2U &&
              final_response.action_count == 3U,
          "after the configured re-raise only fold, call and all-in remain");
  require(game.states()[after_reraise].last_full_raise_increment.units() == 50'000,
          "raise to 10.5a keeps the 5a full-raise increment");

  // Limp: BTN may check or bet the open targets.
  std::uint32_t after_limp = pb::no_node;
  for (const auto &edge : game.edges_of(root)) {
    if (edge.action.type == gtosd::ActionType::Call) {
      after_limp = edge.child;
    }
  }
  const auto &limp = game.nodes()[after_limp];
  require(limp.kind == pb::NodeKind::Decision && limp.actor == 1U && limp.level == 0U &&
              limp.action_count == 4U,
          "BTN after a limp: check, two bets, all-in");
  require(has_edge(game, after_limp, gtosd::ActionType::Check, 0) &&
              has_edge(game, after_limp, gtosd::ActionType::Bet, 50'000) &&
              has_edge(game, after_limp, gtosd::ActionType::Bet, 90'000),
          "limp branch mirrors the open targets 6a and 10a");
}

void test_structure_and_transitions(const pb::CompiledGame &game, const bool preflop_only) {
  const auto &nodes = game.nodes();
  const auto &states = game.states();
  const auto &config = game.config();
  require(nodes.size() == states.size(), "one public state per node");
  require(nodes.size() == game.stats().node_count, "node count matches");
  for (const auto &node : nodes) {
    const auto &state = states[node.id];
    require(node.id == static_cast<std::uint32_t>(&node - nodes.data()), "ids are positional");
    require(node.subtree_end > node.id && node.subtree_end <= nodes.size(), "subtree range");
    require(node.active_mask == state.active_players_mask, "active mask mirrors the state");
    require(node.street == state.street, "street mirrors the state");
    if (node.parent != pb::no_node) {
      const auto &parent = nodes[node.parent];
      require(node.parent < node.id && parent.subtree_end >= node.subtree_end,
              "parent precedes the node and covers its subtree");
      require(node.depth == parent.depth + 1U, "depth grows by one along edges");
    } else {
      require(node.id == game.root() && node.depth == 0U, "only the root has no parent");
    }
    switch (node.kind) {
    case pb::NodeKind::Decision:
      require(state.status == gtosd::HandStatus::InProgress && node.actor == state.player_to_act,
              "decision nodes are in-progress states of the actor");
      break;
    case pb::NodeKind::Chance:
      require(state.status == gtosd::HandStatus::StreetComplete && node.actor == pb::no_player,
              "chance nodes are street-complete states");
      break;
    case pb::NodeKind::TerminalFold:
      require(state.status == gtosd::HandStatus::Folded && node.action_count == 0U &&
                  std::popcount(state.active_players_mask) == 1,
              "fold terminals leave one active player");
      break;
    case pb::NodeKind::TerminalShowdown:
      require((state.status == gtosd::HandStatus::Showdown && node.remaining_board_cards == 0U) ||
                  (state.status == gtosd::HandStatus::AllInRunout &&
                   node.remaining_board_cards ==
                       (state.street == gtosd::Street::Preflop  ? 5U
                        : state.street == gtosd::Street::Flop   ? 2U
                        : state.street == gtosd::Street::Turn   ? 1U
                                                                : 0U)),
              "showdown terminals record the board cards still to deal");
      require(node.action_count == 0U, "terminals have no edges");
      break;
    }
    if (node.street != gtosd::Street::Preflop) {
      require(node.postflop_entry != pb::no_entry, "postflop nodes name their entry");
      const auto entry = game.postflop_entries()[node.postflop_entry];
      require(entry < node.id && nodes[entry].subtree_end > node.id,
              "postflop nodes lie inside the subtree of their entry");
    } else if (node.kind == pb::NodeKind::Chance) {
      require(node.postflop_entry != pb::no_entry &&
                  game.postflop_entries()[node.postflop_entry] == node.id,
              "preflop chance nodes are the postflop entries");
    } else {
      require(node.postflop_entry == pb::no_entry, "preflop nodes have no entry");
    }

    // Children are laid out in preorder right after the node, each subtree contiguous.
    std::uint32_t expected_next = node.id + 1U;
    const auto edges = game.edges_of(node.id);
    require(edges.size() == node.action_count, "edge span matches the action count");
    for (const auto &edge : edges) {
      require(edge.child == expected_next, "children follow in preorder");
      const auto &child = nodes[edge.child];
      require(child.parent == node.id, "child points back to the parent");
      expected_next = child.subtree_end;
    }
    require(expected_next == node.subtree_end, "subtree ends after the last child subtree");

    if (node.kind == pb::NodeKind::Decision) {
      const auto action_config = pb::action_config_at(config, state, node.level);
      require(action_config.has_value(), "action configuration resolves");
      const auto legal = gtosd::legal_actions(state, action_config.value());
      require(legal.has_value() && legal.value().size() == edges.size(),
              "compiled actions match legal_actions in number");
      for (std::size_t index = 0; index < edges.size(); ++index) {
        require(edges[index].action == legal.value()[index],
                "compiled actions match legal_actions in order and content");
        if (config.player_count == 2U) {
          const auto next = gtosd::apply_action(state, edges[index].action, action_config.value());
          require(next.has_value() && next.value() == states[edges[index].child],
                  "compiled transition equals the core apply_action");
        } else {
          const auto next =
              pb::apply_action_at(state, edges[index].action, action_config.value());
          require(next.has_value() && next.value() == states[edges[index].child],
                  "compiled transition equals the multiway transition");
        }
        const auto child_level = node.level + (pb::is_aggressive(edges[index].action) ? 1U : 0U);
        require(nodes[edges[index].child].level == child_level ||
                    nodes[edges[index].child].street != node.street,
                "aggression level counts bets, raises and all-in raises");
      }
    } else if (node.kind == pb::NodeKind::Chance) {
      if (preflop_only && node.street == gtosd::Street::Preflop) {
        require(edges.empty(), "preflop-only compilation stops at the entries");
      } else {
        require(edges.size() == 1U, "a chance node has one street transition");
        if (config.player_count == 2U) {
          const auto next = gtosd::advance_street(state);
          require(next.has_value() && next.value() == states[edges[0].child],
                  "street transition equals the core advance_street");
        } else {
          const auto next = pb::advance_to_next_street(state);
          require(next.has_value() && next.value() == states[edges[0].child],
                  "street transition equals the multiway advance");
        }
        require(nodes[edges[0].child].level == 0U, "a new street starts at aggression level 0");
      }
    }
  }
}

void test_payoffs(const pb::CompiledGame &game) {
  const auto &config = game.config();
  const auto players = config.player_count;
  std::uint64_t terminals = 0U;
  for (const auto &node : game.nodes()) {
    const auto &state = game.states()[node.id];
    if (node.kind == pb::NodeKind::TerminalFold) {
      ++terminals;
      const auto settlement = gtosd::settle_terminal(state, config.rake);
      require(settlement.has_value(), "fold terminal settles");
      const auto payoffs = game.fold_payoffs(node.id);
      std::int64_t total = 0;
      for (std::uint8_t player = 0; player < players; ++player) {
        require(payoffs[player] == settlement.value().payoff_units[player],
                "fold payoff equals settle_terminal");
        total += payoffs[player];
        const bool winner = (state.terminal_winner_mask >> player) & 1U;
        require(winner ? payoffs[player] > 0 : payoffs[player] < 0,
                "the remaining player wins, every other player loses at least the ante");
      }
      require(total == 0, "fold payoffs are zero-sum without rake");
    } else if (node.kind == pb::NodeKind::TerminalShowdown) {
      ++terminals;
      require(pb::CompiledGame::showdown_rows(node.active_mask) ==
                  (std::uint32_t{1} << std::popcount(node.active_mask)) - 1U,
              "showdown rows count the non-empty winner subsets");
      std::uint32_t rows_seen = 0U;
      for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
        if ((winners & static_cast<std::uint8_t>(~node.active_mask)) != 0U) {
          continue;
        }
        require(pb::CompiledGame::showdown_row(node.active_mask, winners) == rows_seen,
                "winner subsets are enumerated in increasing order");
        ++rows_seen;
        const auto settlement = gtosd::settle_terminal(state, config.rake, winners);
        require(settlement.has_value(), "showdown terminal settles");
        const auto payoffs = game.showdown_payoffs(node.id, winners);
        std::int64_t total = 0;
        for (std::uint8_t player = 0; player < players; ++player) {
          require(payoffs[player] == settlement.value().payoff_units[player],
                  "showdown payoff equals settle_terminal");
          total += payoffs[player];
          const bool winner = (winners >> player) & 1U;
          const bool active = (node.active_mask >> player) & 1U;
          if (winner && std::popcount(winners) == 1) {
            require(payoffs[player] > 0, "a sole winner profits");
          } else if (active && !winner) {
            require(payoffs[player] < 0, "an active loser pays");
          }
        }
        require(total == 0, "showdown payoffs are zero-sum without rake");
      }
      require(rows_seen == pb::CompiledGame::showdown_rows(node.active_mask), "all rows settled");
    }
  }
  require(terminals == game.stats().terminal_folds + game.stats().terminal_showdowns,
          "terminal count matches the statistics");
}

void test_co40_postflop_counts(const pb::CompiledGame &game) {
  const auto &stats = game.stats();
  // Measured on the legacy skeleton analyzer (gtosd_hu_preflop_tree, 2026-09-15):
  // represented 27,012, decisions 10,060, action edges 25,944, chance frontiers
  // 1,059, folds 7,942, showdowns 6,715, all-in runouts 1,236, four raises at
  // most. The 30,324 / 11,308 quoted by older documents predate the current
  // betting rules and are not reproduced by the legacy code either.
  require(stats.postflop_represented_nodes == 27'012U,
          "CO40 postflop skeleton has 27,012 represented nodes");
  require(stats.postflop_decisions == 10'060U, "CO40 postflop skeleton has 10,060 decisions");
  require(stats.postflop_action_edges == 25'944U, "CO40 postflop skeleton has 25,944 action edges");
  require(stats.postflop_chance_frontiers == 1'059U && stats.postflop_terminal_folds == 7'942U &&
              stats.postflop_terminal_showdowns == 6'715U &&
              stats.postflop_terminal_all_in_runouts == 1'236U,
          "CO40 postflop terminal and chance classes match the legacy analyzer");
  require(stats.maximum_raise_count == 4U, "CO40 reaches at most four raises on a street");
  require(stats.postflop_decisions_by_street[0] + stats.postflop_decisions_by_street[1] +
                  stats.postflop_decisions_by_street[2] ==
              stats.postflop_decisions,
          "decisions by street add up");
  require(stats.postflop_chance_frontiers >= 9U && stats.postflop_terminal_showdowns > 0U &&
              stats.postflop_terminal_all_in_runouts > 0U && stats.postflop_terminal_folds > 0U,
          "postflop skeleton reaches every terminal class");
  require(stats.postflop_represented_nodes ==
              stats.postflop_decisions + stats.postflop_chance_frontiers +
                  stats.postflop_terminal_folds + stats.postflop_terminal_showdowns +
                  stats.postflop_terminal_all_in_runouts,
          "postflop node classes partition the represented nodes");
  require(stats.node_count == stats.preflop_nodes + stats.postflop_represented_nodes -
                                  stats.postflop_entries,
          "whole tree = preflop part + postflop subtrees (entries counted once)");
  require(stats.maximum_raise_count < gtosd::maximum_core_raise_depth,
          "raise sequences terminate naturally before the core safety limit");
  std::cout << "CO40 postflop: represented=" << stats.postflop_represented_nodes
            << " decisions=" << stats.postflop_decisions
            << " flop=" << stats.postflop_decisions_by_street[0]
            << " turn=" << stats.postflop_decisions_by_street[1]
            << " river=" << stats.postflop_decisions_by_street[2]
            << " chance=" << stats.postflop_chance_frontiers
            << " folds=" << stats.postflop_terminal_folds
            << " showdowns=" << stats.postflop_terminal_showdowns
            << " runouts=" << stats.postflop_terminal_all_in_runouts
            << " max_depth=" << stats.maximum_depth
            << " max_raises=" << static_cast<unsigned>(stats.maximum_raise_count)
            << " seconds=" << stats.compile_seconds << '\n';
}

void test_layout(const pb::CompiledGame &game) {
  for (const auto [flop, turn, river] :
       {std::array<std::uint16_t, 3>{200U, 500U, 1'000U},
        std::array<std::uint16_t, 3>{500U, 1'000U, 2'000U}}) {
    const auto layout = pb::layout_state(game, flop, turn, river);
    require(layout.offsets.size() == game.nodes().size(), "one offset per node");
    std::uint64_t expected = 0U;
    std::array<std::uint64_t, 4> by_street{};
    for (const auto &node : game.nodes()) {
      if (node.kind != pb::NodeKind::Decision) {
        require(layout.offsets[node.id] == pb::no_offset, "non-decisions have no state");
        continue;
      }
      require(layout.offsets[node.id] == expected, "decision offsets are contiguous");
      const auto rows = pb::StateLayout::rows_for(node.street, flop, turn, river);
      require(rows == (node.street == gtosd::Street::Preflop ? 81U
                       : node.street == gtosd::Street::Flop  ? flop
                       : node.street == gtosd::Street::Turn  ? turn
                                                              : river),
              "rows per street: 81 classes preflop, the capacity postflop");
      expected += static_cast<std::uint64_t>(rows) * node.action_count;
      by_street[static_cast<std::size_t>(node.street)] +=
          static_cast<std::uint64_t>(rows) * node.action_count;
    }
    require(layout.entries == expected && layout.entries_by_street == by_street,
            "layout totals equal the sum over decisions");
    require(layout.state_bytes() == 2U * 8U * layout.entries, "state bytes: R and S in double");
    std::cout << game.config().id << " layout " << flop << '/' << turn << '/' << river
              << ": entries=" << layout.entries << " preflop=" << layout.entries_by_street[0]
              << " flop=" << layout.entries_by_street[1] << " turn=" << layout.entries_by_street[2]
              << " river=" << layout.entries_by_street[3]
              << " state_bytes=" << layout.state_bytes() << '\n';
  }
  const auto baseline = pb::layout_state(game, 200U, 500U, 1'000U);
  const auto alternative = pb::layout_state(game, 500U, 1'000U, 2'000U);
  require(alternative.entries > baseline.entries, "larger capacities need more state");
}

void test_hu10_trees(const pb::CompiledGame &co40) {
  const auto full = compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
  const auto reduced = compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  for (const auto *game : {&full, &reduced}) {
    require(game->stats().preflop_nodes > 0U && game->stats().preflop_decisions > 0U &&
                game->stats().postflop_entries > 0U &&
                game->stats().preflop_nodes == full.stats().preflop_nodes &&
                game->stats().preflop_decisions == full.stats().preflop_decisions &&
                game->stats().postflop_entries == full.stats().postflop_entries,
            "HU10 full and reduced share the preflop part (one full-pot open plus all-in)");
    test_structure_and_transitions(*game, false);
    test_payoffs(*game);
  }
  require(reduced.stats().node_count < full.stats().node_count,
          "the reduced action set compiles to a smaller tree");
  require(full.fingerprint() != reduced.fingerprint() && full.fingerprint() != co40.fingerprint(),
          "tree fingerprints separate the fixtures");
  require(legacy_preflop_fingerprint(full) == legacy_preflop_fingerprint(reduced),
          "HU10 full and reduced share the preflop part");
  require(legacy_preflop_fingerprint(full) != legacy_preflop_fingerprint(co40),
          "HU10 and CO40 differ in the preflop part");
  const auto recompiled = compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
  require(recompiled.fingerprint() == full.fingerprint(), "compilation is deterministic");
  std::cout << "HU10 full: nodes=" << full.stats().node_count
            << " postflop_represented=" << full.stats().postflop_represented_nodes
            << " decisions=" << full.stats().decision_nodes
            << " seconds=" << full.stats().compile_seconds
            << "; reduced: nodes=" << reduced.stats().node_count
            << " decisions=" << reduced.stats().decision_nodes << '\n';
  test_layout(full);
  test_layout(reduced);
}

pb::GameConfig three_way_config() {
  auto config = load_fixture("preflop_blueprint_co40_v1.json");
  config.id = "PREFLOP-BLUEPRINT-3WAY40-READINESS";
  config.player_count = 3U;
  config.positions = {"UTG", "CO", "BTN"};
  require(pb::validate_game_config(config).has_value(), "three-way configuration validates");
  return config;
}

std::uint32_t follow(const pb::CompiledGame &game, std::uint32_t node, const gtosd::ActionType type,
                     const std::int64_t amount_units) {
  for (const auto &edge : game.edges_of(node)) {
    if (edge.action.type == type && edge.action.amount.units() == amount_units) {
      return edge.child;
    }
  }
  throw std::runtime_error("edge not found");
}

void test_three_way_readiness() {
  const auto config = three_way_config();
  const auto root = pb::make_preflop_state(config);
  require(root.has_value(), "three-way preflop state builds");
  const auto &state = root.value();
  require(state.player_count == 3U && state.player_to_act == 0U &&
              state.initial_pot.units() == 30'000 && state.pot.units() == 40'000 &&
              state.committed_this_street[2].units() == 10'000 &&
              state.committed_this_street[0].units() == 0 &&
              state.remaining_stacks[0].units() == 390'000 &&
              state.remaining_stacks[2].units() == 380'000 && state.active_players_mask == 0b111U,
          "three antes dead, BTN posts the live blind, UTG acts first");

  pb::CompileOptions options;
  options.preflop_only = true;
  const auto game = compile(config, options);
  const auto &stats = game.stats();
  require(stats.preflop_nodes == stats.node_count && stats.postflop_entries > 9U &&
              stats.preflop_decisions > 20U,
          "three-way preflop tree is larger than the two-player one");
  test_structure_and_transitions(game, true);
  test_payoffs(game);
  for (const auto entry : game.postflop_entries()) {
    const auto &entry_state = game.states()[entry];
    require(std::popcount(entry_state.active_players_mask) >= 2, "entries keep two or more players");
    const auto next = pb::advance_to_next_street(entry_state);
    require(next.has_value() && next.value().street == gtosd::Street::Flop &&
                ((next.value().active_players_mask >> next.value().player_to_act) & 1U) == 1U &&
                ((next.value().all_in_players_mask >> next.value().player_to_act) & 1U) == 0U,
            "the flop starts with the first active player who is not all-in");
  }

  // UTG raises to 6a, CO and BTN fold: UTG wins the antes and the blind, 5a return.
  const auto after_raise = follow(game, game.root(), gtosd::ActionType::Raise, 60'000);
  require(game.nodes()[after_raise].actor == 1U, "CO acts after the UTG raise");
  const auto after_co_fold = follow(game, after_raise, gtosd::ActionType::Fold, 0);
  const auto &co_folded = game.states()[after_co_fold];
  require(co_folded.status == gtosd::HandStatus::InProgress && co_folded.player_to_act == 2U &&
              co_folded.active_players_mask == 0b101U,
          "after a fold two players remain and the BTN acts");
  const auto after_btn_fold = follow(game, after_co_fold, gtosd::ActionType::Fold, 0);
  const auto &won = game.states()[after_btn_fold];
  require(won.status == gtosd::HandStatus::Folded && won.terminal_winner_mask == 0b001U &&
              won.returned_uncalled.units() == 50'000 && won.pot.units() == 50'000,
          "UTG wins uncontested and the uncalled 5a return");
  const auto payoffs = game.fold_payoffs(after_btn_fold);
  require(payoffs[0] == 30'000 && payoffs[1] == -10'000 && payoffs[2] == -20'000,
          "UTG +3a, CO -1a, BTN -2a");

  // UTG folds, CO limps, BTN checks: the flop starts with the CO.
  const auto after_utg_fold = follow(game, game.root(), gtosd::ActionType::Fold, 0);
  const auto after_co_call = follow(game, after_utg_fold, gtosd::ActionType::Call, 10'000);
  const auto after_btn_check = follow(game, after_co_call, gtosd::ActionType::Check, 0);
  const auto &entry_state = game.states()[after_btn_check];
  require(game.nodes()[after_btn_check].kind == pb::NodeKind::Chance &&
              entry_state.active_players_mask == 0b110U,
          "UTG fold, CO limp, BTN check reaches a two-player entry");
  const auto flop = pb::advance_to_next_street(entry_state);
  require(flop.has_value() && flop.value().player_to_act == 1U, "CO is first to act on that flop");

  // UTG limps, CO limps, BTN checks: a three-way entry led by UTG.
  const auto after_utg_call = follow(game, game.root(), gtosd::ActionType::Call, 10'000);
  const auto after_co_call_3 = follow(game, after_utg_call, gtosd::ActionType::Call, 10'000);
  const auto after_btn_check_3 = follow(game, after_co_call_3, gtosd::ActionType::Check, 0);
  const auto flop_3 = pb::advance_to_next_street(game.states()[after_btn_check_3]);
  require(game.nodes()[after_btn_check_3].kind == pb::NodeKind::Chance && flop_3.has_value() &&
              flop_3.value().player_to_act == 0U && flop_3.value().active_players_mask == 0b111U,
          "three-way limped pot: UTG acts first on the flop");
  std::cout << "3-way preflop: nodes=" << stats.node_count
            << " decisions=" << stats.preflop_decisions << " entries=" << stats.postflop_entries
            << " folds=" << stats.preflop_terminal_folds
            << " all_in_runouts=" << stats.preflop_all_in_runouts
            << " fingerprint=" << game.fingerprint() << '\n';
}

} // namespace

int main() {
  try {
    test_root_state_matches_core();
    const auto co40 = compile(load_fixture("preflop_blueprint_co40_v1.json"));
    test_co40_preflop_tree(co40);
    test_structure_and_transitions(co40, false);
    test_payoffs(co40);
    test_co40_postflop_counts(co40);
    test_layout(co40);
    test_hu10_trees(co40);
    test_three_way_readiness();
    std::cout << "CO40 tree fingerprint " << co40.fingerprint() << '\n';
    std::cout << "PREFLOP_BLUEPRINT_GAME_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_GAME_TESTS=FAIL " << error.what() << " after " << assertions
              << " assertions\n";
    return 1;
  }
}
