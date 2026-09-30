#include "gtosd/core/game.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

pb::GameConfig load_config(const std::filesystem::path &relative_path) {
  const auto path = std::filesystem::path(GTOSD_SOURCE_DIR) / relative_path;
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), "configuration parses: " + relative_path.generic_string());
  return parsed.value();
}

pb::GameConfig load_fixture(const std::string_view name) {
  return load_config(std::filesystem::path("benchmarks") / "fixtures" / std::string(name));
}

// Tree of the MonkerSolver HU 50a charts: open 5a, then fold, call or all-in;
// postflop one pot-sized bet or raise plus the all-in. CO acts first postflop.
pb::GameConfig load_hu50() {
  return load_config(std::filesystem::path("benchmarks") / "monker" / "HU50.json");
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

// Seat of the last player who bet, raised or went all-in on the street before
// the node's street, read off the path from the root instead of the compiler's
// own bookkeeping; pb::no_aggressor on the preflop street, after a street
// without aggression and when that betting precedes the root of a subgame.
std::uint8_t previous_round_aggressor(const pb::CompiledGame &game, const std::uint32_t node) {
  const auto &nodes = game.nodes();
  if (nodes[node].street == gtosd::Street::Preflop) {
    return pb::no_aggressor;
  }
  const auto previous =
      static_cast<gtosd::Street>(static_cast<std::uint8_t>(nodes[node].street) - 1U);
  // Walking up, the first aggressive edge met on the previous street is the
  // last one taken on it.
  for (std::uint32_t child = node; nodes[child].parent != pb::no_node;
       child = nodes[child].parent) {
    const auto &parent = nodes[nodes[child].parent];
    if (parent.kind != pb::NodeKind::Decision || parent.street != previous) {
      continue;
    }
    for (const auto &edge : game.edges_of(parent.id)) {
      if (edge.child == child && pb::is_aggressive(edge.action)) {
        return parent.actor;
      }
    }
  }
  return pb::no_aggressor;
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
  // Tree of 2026-09-17. The user replaced the Monker sizes with the exact
  // pot-raise formula "P + 2B - c" (pot before the action, bet to match, chips
  // the raiser already has in this round) and removed the configured re-raise
  // on the limped branch. One size per spot follows from the formula: both
  // opens are decided at the root, so they would both be 5a.
  //   root      P=3  B=1 c=0 -> open 5a
  //   limp      P=4  B=1 c=1 -> BTN raises to 5a, the same number
  //   vs open   P=8  B=5 c=1 -> response 17a
  // Before the change: two opens 6a/10a, responses 10.5a/14.5a, 58 preflop
  // nodes, 20 decisions, 9 postflop entries, 29 terminals.
  require(stats.preflop_nodes == 28U, "CO40 preflop part has 28 nodes");
  require(stats.preflop_decisions == 10U, "CO40 preflop part has 10 decisions");
  require(stats.postflop_entries == 4U, "CO40 preflop part has 4 postflop entries");
  require(stats.preflop_terminal_folds + stats.preflop_all_in_runouts == 14U,
          "CO40 preflop part has 14 terminals");
  require(game.postflop_entries().size() == 4U, "entry list has 4 ids");
  // No longer the legacy tree: sizes and the limped branch differ on purpose.
  // Frozen so any further change has to be deliberate. The legacy value was
  // fnv1a64:a68337fa567aa2d9.
  require(legacy_preflop_fingerprint(game) == "fnv1a64:c2169c4295026609",
          std::string("CO40 preflop tree matches the frozen 2026-09-17 fingerprint, got ") +
              legacy_preflop_fingerprint(game));

  const auto root = game.root();
  const auto &root_node = game.nodes()[root];
  require(root_node.kind == pb::NodeKind::Decision && root_node.actor == 0U &&
              root_node.action_count == 4U && root_node.level == 0U && !root_node.limped_pot,
          "root: CO decides among four actions");
  require(has_edge(game, root, gtosd::ActionType::Fold, 0) &&
              has_edge(game, root, gtosd::ActionType::Call, 10'000) &&
              has_edge(game, root, gtosd::ActionType::Raise, 50'000) &&
              has_edge(game, root, gtosd::ActionType::AllIn, 390'000),
          "root actions: fold, call 1a, raise to 5a, all-in 39a");

  // BTN facing the open: fold, call, re-raise to 17a, all-in.
  std::uint32_t after_open = pb::no_node;
  for (const auto &edge : game.edges_of(root)) {
    if (edge.action.type == gtosd::ActionType::Raise && edge.action.amount.units() == 50'000) {
      after_open = edge.child;
    }
  }
  require(after_open != pb::no_node, "raise to 5a child exists");
  const auto &open_response = game.nodes()[after_open];
  require(open_response.kind == pb::NodeKind::Decision && open_response.actor == 1U &&
              open_response.level == 1U && !open_response.limped_pot &&
              open_response.action_count == 4U,
          "BTN facing 5a: four actions at aggression level 1, not the limped branch");
  // Edge amounts are the chips the actor adds, so BTN reaching 17a adds 16a
  // over its 1a blind.
  require(has_edge(game, after_open, gtosd::ActionType::Raise, 160'000),
          "BTN facing 5a may raise to 17a: it owes 4a and the pot after the call is 12a");
  std::uint32_t after_reraise = pb::no_node;
  for (const auto &edge : game.edges_of(after_open)) {
    if (edge.action.type == gtosd::ActionType::Raise) {
      after_reraise = edge.child;
    }
  }
  const auto &final_response = game.nodes()[after_reraise];
  require(final_response.kind == pb::NodeKind::Decision && final_response.level == 2U &&
              final_response.action_count == 3U,
          "after the configured re-raise only fold, call and all-in remain");

  // Limp: BTN may check, bet to the open target, or shove.
  std::uint32_t after_limp = pb::no_node;
  for (const auto &edge : game.edges_of(root)) {
    if (edge.action.type == gtosd::ActionType::Call) {
      after_limp = edge.child;
    }
  }
  const auto &limp = game.nodes()[after_limp];
  require(limp.kind == pb::NodeKind::Decision && limp.actor == 1U && limp.level == 0U &&
              limp.limped_pot && limp.action_count == 3U,
          "BTN after a limp: check, one bet, all-in");
  require(has_edge(game, after_limp, gtosd::ActionType::Check, 0) &&
              has_edge(game, after_limp, gtosd::ActionType::Bet, 40'000),
          "BTN bets 4a over its 1a blind, reaching 5a: the exact pot raise on a limp");

  // The limper facing that bet has no configured re-raise: the fixture carries
  // an empty limp_response_target_units. The same aggression level in the open
  // branch still offers the 17a response, so the two branches are distinct
  // although their public states coincide up to which seat holds which bet.
  for (const auto &edge : game.edges_of(after_limp)) {
    if (edge.action.type != gtosd::ActionType::Bet) {
      continue;
    }
    const auto &responder = game.nodes()[edge.child];
    require(responder.kind == pb::NodeKind::Decision && responder.actor == 0U &&
                responder.level == 1U && responder.limped_pot,
            "the limper responds at level 1 on the limped branch");
    require(responder.action_count == 3U,
            "limped branch response: fold, call and all-in only");
    require(!has_edge(game, edge.child, gtosd::ActionType::Raise, 160'000),
            "no configured re-raise survives on the limped branch");
    require(game.states()[edge.child].pot == game.states()[after_open].pot &&
                game.states()[edge.child].current_bet == game.states()[after_open].current_bet,
            "limped and open branches reach the same pot and bet, only the seats differ");
  }
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
      const auto action_config = pb::action_config_at(config, state, node.level, node.limped_pot,
                                                      previous_round_aggressor(game, node.id));
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
      require(total == -settlement.value().rake.units() && (config.rake.enabled || total == 0),
              "fold payoffs sum to minus the rake (zero-sum without rake)");
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
        require(total == -settlement.value().rake.units() && (config.rake.enabled || total == 0),
                "showdown payoffs sum to minus the rake (zero-sum without rake)");
      }
      require(rows_seen == pb::CompiledGame::showdown_rows(node.active_mask), "all rows settled");
    }
  }
  require(terminals == game.stats().terminal_folds + game.stats().terminal_showdowns,
          "terminal count matches the statistics");
}

void test_co40_postflop_counts(const pb::CompiledGame &game) {
  const auto &stats = game.stats();
  // The legacy skeleton analyzer (gtosd_hu_preflop_tree, 2026-09-15) measured
  // represented 27,012, decisions 10,060, action edges 25,944, chance frontiers
  // 1,059, folds 7,942, showdowns 6,715, all-in runouts 1,236 on the tree that
  // still had the configured re-raise on the limped branch. The user removed
  // that branch on 2026-09-17, which drops the two postflop entries it owned;
  // the counts below are the current tree and no longer the legacy ones. The
  // 30,324 / 11,308 quoted by older documents predate the current betting
  // rules and were not reproduced by the legacy code either.
  require(stats.postflop_represented_nodes == 26'854U,
          "CO40 postflop skeleton has 26,854 represented nodes");
  require(stats.postflop_decisions == 9'948U, "CO40 postflop skeleton has 9,948 decisions");
  require(stats.postflop_action_edges == 25'852U, "CO40 postflop skeleton has 25,852 action edges");
  require(stats.postflop_chance_frontiers == 998U && stats.postflop_terminal_folds == 7'952U &&
              stats.postflop_terminal_showdowns == 6'812U &&
              stats.postflop_terminal_all_in_runouts == 1'144U,
          "CO40 postflop terminal and chance classes match the 2026-09-17 tree");
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

  // UTG raises to 5a (the single open target since 2026-09-17), CO and BTN
  // fold: UTG wins the three antes and the blind, 4a return.
  const auto after_raise = follow(game, game.root(), gtosd::ActionType::Raise, 50'000);
  require(game.nodes()[after_raise].actor == 1U, "CO acts after the UTG raise");
  const auto after_co_fold = follow(game, after_raise, gtosd::ActionType::Fold, 0);
  const auto &co_folded = game.states()[after_co_fold];
  require(co_folded.status == gtosd::HandStatus::InProgress && co_folded.player_to_act == 2U &&
              co_folded.active_players_mask == 0b101U,
          "after a fold two players remain and the BTN acts");
  const auto after_btn_fold = follow(game, after_co_fold, gtosd::ActionType::Fold, 0);
  const auto &won = game.states()[after_btn_fold];
  require(won.status == gtosd::HandStatus::Folded && won.terminal_winner_mask == 0b001U &&
              won.returned_uncalled.units() == 40'000 && won.pot.units() == 50'000,
          "UTG wins uncontested and the uncalled 4a return");
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

struct Step {
  gtosd::ActionType type{gtosd::ActionType::Check};
  std::int64_t amount_units{0};
};

// Follows a line of actions from the root, dealing the street transitions on
// the way, and returns the node reached.
std::uint32_t walk(const pb::CompiledGame &game, const std::vector<Step> &line) {
  auto node = game.root();
  for (const auto &step : line) {
    node = follow(game, node, step.type, step.amount_units);
    if (game.nodes()[node].kind == pb::NodeKind::Chance) {
      node = game.edges_of(node)[0].child;
    }
  }
  return node;
}

bool check_only(const pb::CompiledGame &game, const std::uint32_t node) {
  const auto edges = game.edges_of(node);
  return game.nodes()[node].kind == pb::NodeKind::Decision && edges.size() == 1U &&
         edges[0].action.type == gtosd::ActionType::Check;
}

std::uint64_t check_only_decisions(const pb::CompiledGame &game) {
  std::uint64_t count = 0U;
  for (const auto &node : game.nodes()) {
    count += check_only(game, node.id) ? 1U : 0U;
  }
  return count;
}

// Tree of the MonkerSolver 3-way 50a charts: a pot-sized first raise, then
// fold, call or all-in; postflop the HU50 G1 rules (one pot size, the all-in,
// donk bets).
pb::GameConfig load_three_way50() {
  return load_config(std::filesystem::path("benchmarks") / "monker" / "3WAY50_donk.json");
}

gtosd::PotPercentage pot_raise() { return gtosd::PotPercentage::from_basis_points(10'000).value(); }

// preflop_open_sizes_basis_points: optional list, serialized only when set and
// mutually exclusive with open_target_units.
void test_open_sizes_config_key() {
  // Every configuration written before the key keeps its serialization; their
  // frozen fingerprints are checked by test_fingerprints_without_the_cap.
  for (const std::string_view path :
       {"benchmarks/fixtures/preflop_blueprint_co40_v1.json",
        "benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json",
        "benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json", "benchmarks/monker/HU50.json",
        "benchmarks/monker/HU50_step2_donk.json"}) {
    const auto config = load_config(std::filesystem::path(path));
    const auto serialized = pb::serialize_game_config_json(config);
    require(config.open_sizes.empty() && serialized.find("preflop_open_sizes") == std::string::npos,
            std::string(path) + " has live open targets and serializes no open size");
  }

  const auto targets = load_hu50();
  auto pot = targets;
  pot.open_targets.clear();
  pot.open_sizes = {pot_raise()};
  require(pb::validate_game_config(pot).has_value(), "pot mode validates");
  require(pot != targets &&
              pb::game_config_fingerprint(pot) != pb::game_config_fingerprint(targets),
          "the open sizes enter operator== and the configuration fingerprint");
  const auto pot_json = pb::serialize_game_config_json(pot);
  const auto key_position = pot_json.find("\"preflop_open_sizes_basis_points\": [");
  require(key_position != std::string::npos &&
              pot_json.find("\"open_target_units\": []") != std::string::npos,
          "pot mode serializes the sizes next to an empty target list");
  const auto reparsed = pb::parse_game_config_json(pot_json);
  require(reparsed.has_value() && reparsed.value() == pot &&
              pb::game_config_fingerprint(reparsed.value()) == pb::game_config_fingerprint(pot),
          "the open sizes survive a serialization round trip");

  const auto value_end = pot_json.find(']', key_position) + 1U;
  const auto parse_with_value = [&](const std::string_view value) {
    auto text = pot_json;
    text.replace(key_position, value_end - key_position,
                 std::string("\"preflop_open_sizes_basis_points\": ") + std::string(value));
    return pb::parse_game_config_json(text);
  };
  for (const auto [value, count] :
       {std::pair<std::string_view, std::size_t>{"[5000, 10000]", 2U},
        std::pair<std::string_view, std::size_t>{"[1, 50000, 100000]", 3U}}) {
    const auto parsed = parse_with_value(value);
    require(parsed.has_value() && parsed.value().open_sizes.size() == count,
            "up to three increasing sizes from 1 to 100000 basis points are accepted");
  }
  for (const std::string_view invalid :
       {"[0]", "[10000, 10000]", "[10000, 5000]", "[100001]", "[-1]", "[10000.0]", "[\"10000\"]",
        "[null]", "10000", "null", "true", "{}"}) {
    const auto parsed = parse_with_value(invalid);
    require(!parsed.has_value() && parsed.error() == pb::ConfigError::InvalidValue,
            "zero, out of range, not increasing or not a list of integers: an invalid value, " +
                std::string(invalid));
  }
  for (const std::string_view invalid : {"[]", "[2500, 5000, 7500, 10000]"}) {
    const auto parsed = parse_with_value(invalid);
    require(!parsed.has_value() && parsed.error() == pb::ConfigError::InvalidStructure,
            "an empty list or more than three sizes is an invalid structure: " +
                std::string(invalid));
  }

  // Exactly one kind of first raise, and no re-raise target in pot mode.
  auto both = pot;
  both.open_targets = {antes(5)};
  auto neither = pot;
  neither.open_sizes.clear();
  auto response = pot;
  response.response_targets = {antes(7)};
  auto limp_response = pot;
  limp_response.limp_response_targets = std::vector<gtosd::Money>{antes(7)};
  for (const auto *invalid : {&both, &neither, &response, &limp_response}) {
    const auto valid = pb::validate_game_config(*invalid);
    require(!valid.has_value() && valid.error() == pb::ConfigError::InvalidStructure,
            "open targets and open sizes exclude each other, and pot mode has no response target");
  }
  auto limp_all_in = pot;
  limp_all_in.limp_response_targets = std::vector<gtosd::Money>{};
  require(pb::validate_game_config(limp_all_in).has_value(),
          "an empty limped-pot response list (all-in only) is allowed in pot mode");

  // The 3-way fixtures: pot mode, and their variants with their own ids.
  const auto three_way = load_three_way50();
  require(three_way.player_count == 3U &&
              three_way.positions == std::vector<std::string>{"UTG", "CO", "BTN"} &&
              three_way.open_targets.empty() &&
              three_way.open_sizes == std::vector<gtosd::PotPercentage>{pot_raise()} &&
              three_way.response_targets.empty() && !three_way.limp_response_targets.has_value(),
          "3WAY50_donk is in pot mode with a single 100 % open size");
  auto capped = three_way;
  capped.id = "MONKER-3WAY50-DONK-ALLIN5X-001";
  capped.postflop_all_in_max_pot = gtosd::PotPercentage::from_basis_points(50'000).value();
  require(load_config("benchmarks/monker/3WAY50_donk_allin5x.json") == capped,
          "3WAY50_donk_allin5x is 3WAY50_donk with the 5x cap");
  auto deeper = three_way;
  deeper.id = "MONKER-3WAY100-DONK-001";
  deeper.effective_stack = antes(100);
  require(load_config("benchmarks/monker/3WAY100_donk.json") == deeper,
          "3WAY100_donk is 3WAY50_donk with 100a stacks");
}

// Pot mode reproduces the HU50 target tree: the pot-sized open is 5a (pot 3a
// plus the 1a call, on top of the call) and the BTN's pot bet over the limp
// adds 4a to its blind, exactly open_target_units [50000]. Only the
// fingerprints move, because the configuration is part of them.
void test_hu50_pot_mode_equivalence() {
  const auto targets_config = load_config("benchmarks/monker/HU50_step2_donk.json");
  auto pot_config = targets_config;
  pot_config.open_targets.clear();
  pot_config.open_sizes = {pot_raise()};
  const auto targets = compile(targets_config);
  const auto pot = compile(pot_config);
  require(targets.nodes().size() == 571U && pot.nodes().size() == targets.nodes().size(),
          "pot mode compiles the 571 nodes of HU50_step2_donk");
  for (const auto &node : pot.nodes()) {
    const auto &other = targets.nodes()[node.id];
    require(node.kind == other.kind && node.level == other.level &&
                node.limped_pot == other.limped_pot && node.actor == other.actor &&
                node.action_count == other.action_count &&
                gtosd::serialize_public_state(pot.states()[node.id]) ==
                    gtosd::serialize_public_state(targets.states()[node.id]),
            "pot mode: same node kind, level and public state");
    const auto edges = pot.edges_of(node.id);
    const auto other_edges = targets.edges_of(node.id);
    for (std::size_t index = 0; index < edges.size(); ++index) {
      require(edges[index].action.type == other_edges[index].action.type &&
                  edges[index].action.amount == other_edges[index].action.amount &&
                  edges[index].action.all_in_kind == other_edges[index].action.all_in_kind &&
                  edges[index].child == other_edges[index].child,
              "pot mode: same edges, amounts and children");
    }
    if (node.kind == pb::NodeKind::TerminalFold) {
      const auto payoffs = pot.fold_payoffs(node.id);
      const auto other_payoffs = targets.fold_payoffs(node.id);
      require(std::equal(payoffs.begin(), payoffs.end(), other_payoffs.begin()),
              "pot mode: same fold payoffs");
    }
  }
  require(pot.fingerprint() != targets.fingerprint(),
          "the tree fingerprint follows the configuration");
  test_structure_and_transitions(pot, false);
  test_payoffs(pot);
}

// Preflop tree of the MonkerSolver 3-way 50a charts (54 files): a pot-sized
// first raise (6a UTG open, 7a CO isolation over the limp, 6a / 7a BTN raise
// over one / two limps), then fold, call or all-in; limps, over-limps, cold
// calls and multiway calls of an all-in allowed. Amounts are the chips the
// actor adds, in units (1a = 10,000).
void test_three_way_monker_preflop() {
  using gtosd::ActionType;
  pb::CompileOptions options;
  options.preflop_only = true;
  const auto game = compile(load_three_way50(), options);
  const auto &stats = game.stats();
  std::array<std::uint64_t, 4> entries_by_players{};
  for (const auto entry : game.postflop_entries()) {
    ++entries_by_players[static_cast<std::size_t>(std::popcount(game.nodes()[entry].active_mask))];
  }
  require(stats.node_count == 130U && stats.preflop_decisions == 54U &&
              stats.preflop_terminal_folds == 25U && stats.preflop_all_in_runouts == 36U &&
              stats.postflop_entries == 15U && entries_by_players[2] == 11U &&
              entries_by_players[3] == 4U,
          "3-way 50a preflop: 130 nodes, 54 decisions, 25 folds, 36 all-in runouts, 15 entries "
          "(11 two-way, 4 three-way)");
  require(game.fingerprint() == "fnv1a64:1eecd0ede02825d5",
          "the 3-way 50a preflop tree keeps its fingerprint, got " + game.fingerprint());

  const auto root = game.root();
  require(has_edge(game, root, ActionType::Raise, 60'000) &&
              has_edge(game, root, ActionType::AllIn, 490'000),
          "UTG opens to 6a (pot 4a, call 1a: 1 + 5) or shoves 49a");
  const auto utg_limp = follow(game, root, ActionType::Call, 10'000);
  require(has_edge(game, utg_limp, ActionType::Raise, 70'000),
          "CO isolates the UTG limp to 7a (pot 5a, call 1a: 1 + 6)");
  const auto co_fold = follow(game, utg_limp, ActionType::Fold, 0);
  require(game.nodes()[co_fold].actor == 2U && has_edge(game, co_fold, ActionType::Bet, 50'000),
          "after UTG limp and CO fold the BTN raises to 6a (5a on its 1a blind)");
  const auto co_limp = follow(game, utg_limp, ActionType::Call, 10'000);
  require(has_edge(game, co_limp, ActionType::Bet, 60'000),
          "over two limps the BTN raises to 7a (6a on its 1a blind)");
  const auto utg_fold = follow(game, root, ActionType::Fold, 0);
  require(game.nodes()[utg_fold].actor == 1U && has_edge(game, utg_fold, ActionType::Raise, 60'000),
          "after the UTG fold the CO opens to 6a");

  // Every sized raise follows the pot rule, read off the public state; after
  // the first raise only fold, call and all-in remain, and facing an all-in
  // only fold and call.
  const std::vector<ActionType> fold_call_all_in{ActionType::Fold, ActionType::Call,
                                                 ActionType::AllIn};
  const std::vector<ActionType> fold_call{ActionType::Fold, ActionType::Call};
  const std::vector<ActionType> first_raise{ActionType::Fold, ActionType::Call, ActionType::Raise,
                                            ActionType::AllIn};
  const std::vector<ActionType> button_option{ActionType::Check, ActionType::Bet,
                                              ActionType::AllIn};
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    const auto &state = game.states()[node.id];
    std::vector<ActionType> types;
    for (const auto &edge : game.edges_of(node.id)) {
      types.push_back(edge.action.type);
      if (edge.action.type == ActionType::Bet || edge.action.type == ActionType::Raise) {
        const auto to_call = gtosd::amount_to_call(state, state.player_to_act).units();
        require(node.level == 0U &&
                    edge.action.amount.units() - to_call == state.pot.units() + to_call,
                "a sized raise adds the pot after the call on top of the call");
      }
    }
    if (pb::facing_all_in(state)) {
      require(types == fold_call, "facing an all-in: fold or call");
    } else if (node.level >= 1U) {
      require(types == fold_call_all_in, "after the first raise: fold, call or all-in");
    } else if (node.actor == 2U) {
      require(types == button_option, "the BTN over limps: check, the sized raise or the all-in");
    } else {
      require(types == first_raise,
              "before the first raise: fold, limp, the sized raise or the all-in");
    }
  }
  test_structure_and_transitions(game, true);
  test_payoffs(game);
  std::cout << "3-way 50a MonkerSolver preflop: nodes=" << stats.node_count
            << " decisions=" << stats.preflop_decisions << " folds=" << stats.preflop_terminal_folds
            << " all_in_runouts=" << stats.preflop_all_in_runouts
            << " entries=" << stats.postflop_entries << " (" << entries_by_players[2]
            << " two-way, " << entries_by_players[3] << " three-way)"
            << " fingerprint=" << game.fingerprint() << '\n';
}

// The full 3-way 50a trees, first compiled in C++ here, against the counts of
// the independent Python oracle (tools/monker_compare/monker_tree_oracle.py,
// which also reproduces the C++ HU50 counts 493 / 571 / 8,599): nodes,
// decisions and edges per street, and the cells of the compact abstraction
// (15x4 buckets + TX2 turn classes: 34,380 / 268,920 / 67,230 rows). The
// tree fingerprints were frozen when the counts first matched (2026-09-30).
void test_three_way_full_counts() {
  struct Expected {
    std::string_view path;
    std::uint64_t nodes;
    std::array<std::uint64_t, 4> decisions;
    std::array<std::uint64_t, 4> edges;
    std::array<std::uint64_t, 4> cells;
    std::string_view tree;
  };
  for (const auto &expected : {Expected{"benchmarks/monker/3WAY50_donk.json",
                                        7'225U,
                                        {54U, 330U, 936U, 1'802U},
                                        {129U, 790U, 2'078U, 3'886U},
                                        {10'449U, 27'160'200U, 558'815'760U, 261'255'780U},
                                        "fnv1a64:49412deedbe6f6cc"},
                               Expected{"benchmarks/monker/3WAY50_donk_allin5x.json",
                                        7'126U,
                                        {54U, 317U, 923U, 1'789U},
                                        {129U, 757U, 2'045U, 3'853U},
                                        {10'449U, 26'025'660U, 549'941'400U, 259'037'190U},
                                        "fnv1a64:78322185bfa4d03d"}}) {
    const auto game = compile(load_config(std::filesystem::path(expected.path)));
    std::array<std::uint64_t, 4> decisions{};
    std::array<std::uint64_t, 4> edges{};
    for (const auto &node : game.nodes()) {
      if (node.kind == pb::NodeKind::Decision) {
        ++decisions[static_cast<std::size_t>(node.street)];
        edges[static_cast<std::size_t>(node.street)] += node.action_count;
      }
    }
    const auto layout = pb::layout_state(game, 34'380U, 268'920U, 67'230U);
    require(game.stats().node_count == expected.nodes && decisions == expected.decisions &&
                edges == expected.edges && layout.entries_by_street == expected.cells,
            std::string(expected.path) + ": the oracle's nodes, decisions, edges and cells, got " +
                std::to_string(game.stats().node_count) + " nodes, " +
                std::to_string(layout.entries) + " cells");
    require(game.fingerprint() == expected.tree,
            std::string(expected.path) + " keeps its tree fingerprint, got " + game.fingerprint());
    test_structure_and_transitions(game, false);
    test_payoffs(game);
    std::cout << expected.path << ": nodes=" << game.stats().node_count
              << " decisions=" << decisions[0] << '/' << decisions[1] << '/' << decisions[2] << '/'
              << decisions[3] << " edges=" << edges[0] << '/' << edges[1] << '/' << edges[2] << '/'
              << edges[3] << " compact cells=" << layout.entries
              << " fingerprint=" << game.fingerprint() << '\n';
  }
}

// postflop_donk_bets: optional boolean, serialized only when false.
void test_donk_bet_config_key() {
  const auto allowed = load_hu50();
  require(allowed.postflop_donk_bets, "a configuration without the key allows donk bets");
  require(pb::serialize_game_config_json(allowed).find("postflop_donk_bets") == std::string::npos,
          "allowed donk bets are not serialized, so existing fingerprints do not move");

  auto forbidden = allowed;
  forbidden.postflop_donk_bets = false;
  require(forbidden != allowed, "configuration equality sees the flag");
  require(pb::game_config_fingerprint(forbidden) != pb::game_config_fingerprint(allowed),
          "the flag enters the configuration fingerprint");
  const auto forbidden_json = pb::serialize_game_config_json(forbidden);
  constexpr std::string_view serialized_key = "\"postflop_donk_bets\": false";
  const auto key_position = forbidden_json.find(serialized_key);
  require(key_position != std::string::npos, "forbidden donk bets are serialized");
  const auto reparsed = pb::parse_game_config_json(forbidden_json);
  require(reparsed.has_value() && reparsed.value() == forbidden,
          "the flag survives a serialization round trip");

  const auto parse_with_value = [&](const std::string_view value) {
    auto text = forbidden_json;
    text.replace(key_position, serialized_key.size(),
                 std::string("\"postflop_donk_bets\": ") + std::string(value));
    return pb::parse_game_config_json(text);
  };
  const auto explicit_true = parse_with_value("true");
  require(explicit_true.has_value() && explicit_true.value() == allowed &&
              pb::game_config_fingerprint(explicit_true.value()) ==
                  pb::game_config_fingerprint(allowed),
          "an explicit true is the default and keeps the fingerprint");
  for (const std::string_view invalid : {"0", "\"false\"", "null", "[]"}) {
    const auto parsed = parse_with_value(invalid);
    require(!parsed.has_value() && parsed.error() == pb::ConfigError::InvalidValue,
            "a non-boolean postflop_donk_bets is rejected as an invalid value");
  }
}

// Without the key every tree keeps its fingerprint. CO40 and HU10 carry the
// values gtosd_preflop_blueprint_game recorded for the suite on 2026-09-22
// (benchmarks/suite/actions, BENCHMARK_SUITE_INVENTORY_2026-09-21); HU50 was
// first frozen together with the flag.
void test_tree_fingerprints_without_the_flag(const pb::CompiledGame &co40) {
  require(co40.fingerprint() == "fnv1a64:d6c10723d35b9503",
          "CO40 keeps its tree fingerprint, got " + co40.fingerprint());
  const auto hu10_full = compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
  require(hu10_full.fingerprint() == "fnv1a64:bc9e7b35ad8c021d",
          "HU10 full keeps its tree fingerprint, got " + hu10_full.fingerprint());
  const auto hu10_reduced = compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(hu10_reduced.fingerprint() == "fnv1a64:cc5c2f8eea9aac57",
          "HU10 reduced keeps its tree fingerprint, got " + hu10_reduced.fingerprint());
  const auto hu50 = compile(load_hu50());
  require(hu50.fingerprint() == "fnv1a64:d535abd7f76a586f",
          "HU50 keeps its tree fingerprint, got " + hu50.fingerprint());
}

// Donk bets forbidden (postflop_donk_bets false) on the HU50 tree, CO first
// and BTN last postflop. Amounts are the chips the actor adds, in units
// (1a = 10,000).
void test_no_donk_bets() {
  using gtosd::ActionType;
  const auto allowed_config = load_hu50();
  auto forbidden_config = allowed_config;
  forbidden_config.postflop_donk_bets = false;
  const auto allowed = compile(allowed_config);
  const auto forbidden = compile(forbidden_config);
  test_structure_and_transitions(forbidden, false);
  test_payoffs(forbidden);
  require(legacy_preflop_fingerprint(forbidden) == legacy_preflop_fingerprint(allowed) &&
              forbidden.stats().postflop_entries == allowed.stats().postflop_entries,
          "the preflop part does not depend on the flag");

  const std::vector<Step> limp_isolated_called{
      {ActionType::Call, 10'000}, {ActionType::Bet, 40'000}, {ActionType::Call, 40'000}};
  const std::vector<Step> open_called{{ActionType::Raise, 50'000}, {ActionType::Call, 40'000}};
  const std::vector<Step> limp_checked{{ActionType::Call, 10'000}, {ActionType::Check, 0}};

  // (1) CO limps, BTN isolates to 5a, CO calls: the BTN raised last preflop
  // and acts after the CO, so the CO may only check the flop (pot 12a, 44a
  // behind) where donk bets would add the pot bet and the all-in.
  const auto isolated_flop = walk(forbidden, limp_isolated_called);
  require(forbidden.nodes()[isolated_flop].street == gtosd::Street::Flop &&
              forbidden.nodes()[isolated_flop].actor == 0U && check_only(forbidden, isolated_flop),
          "(1) limp, isolation, call: the CO may only check the flop");
  const auto isolated_flop_allowed = walk(allowed, limp_isolated_called);
  require(allowed.nodes()[isolated_flop_allowed].action_count == 3U &&
              has_edge(allowed, isolated_flop_allowed, ActionType::Bet, 120'000) &&
              has_edge(allowed, isolated_flop_allowed, ActionType::AllIn, 440'000),
          "with donk bets the same CO may check, bet the pot or shove");
  const auto isolated_flop_btn = follow(forbidden, isolated_flop, ActionType::Check, 0);
  require(has_edge(forbidden, isolated_flop_btn, ActionType::Bet, 120'000) &&
              has_edge(forbidden, isolated_flop_btn, ActionType::AllIn, 440'000),
          "the aggressor may still bet after the check");

  // (2) CO opens, BTN calls: the aggressor is the CO itself.
  const auto opened_flop = walk(forbidden, open_called);
  require(forbidden.nodes()[opened_flop].actor == 0U &&
              forbidden.nodes()[opened_flop].action_count == 3U &&
              has_edge(forbidden, opened_flop, ActionType::Bet, 120'000) &&
              has_edge(forbidden, opened_flop, ActionType::AllIn, 440'000),
          "(2) open, call: the preflop raiser may bet the flop");

  // (3) CO limps, BTN checks: no preflop aggressor, the blind is not a bet.
  const auto limped_flop = walk(forbidden, limp_checked);
  require(forbidden.nodes()[limped_flop].actor == 0U &&
              has_edge(forbidden, limped_flop, ActionType::Bet, 40'000) &&
              has_edge(forbidden, limped_flop, ActionType::AllIn, 480'000),
          "(3) limp, check: the CO may bet the flop (pot 4a, 48a behind)");

  // (4) BTN bets the flop, CO calls: the CO may only check the turn (pot 36a,
  // 32a behind, so the pot bet is the all-in).
  auto flop_bet_called = open_called;
  flop_bet_called.insert(flop_bet_called.end(), {{ActionType::Check, 0},
                                                 {ActionType::Bet, 120'000},
                                                 {ActionType::Call, 120'000}});
  const auto called_turn = walk(forbidden, flop_bet_called);
  require(forbidden.nodes()[called_turn].street == gtosd::Street::Turn &&
              forbidden.nodes()[called_turn].actor == 0U && check_only(forbidden, called_turn),
          "(4) BTN bets the flop, CO calls: the CO may only check the turn");
  require(has_edge(allowed, walk(allowed, flop_bet_called), ActionType::AllIn, 320'000),
          "with donk bets the same CO may shove the turn");
  require(has_edge(forbidden, follow(forbidden, called_turn, ActionType::Check, 0),
                   ActionType::AllIn, 320'000),
          "the BTN may still shove the turn after the check");

  // (5) Flop checked through after the isolation: the flop had no aggressor,
  // so the preflop raise no longer restricts the CO on the turn.
  auto flop_checked = limp_isolated_called;
  flop_checked.insert(flop_checked.end(), {{ActionType::Check, 0}, {ActionType::Check, 0}});
  const auto checked_turn = walk(forbidden, flop_checked);
  require(forbidden.nodes()[checked_turn].street == gtosd::Street::Turn &&
              forbidden.nodes()[checked_turn].actor == 0U &&
              has_edge(forbidden, checked_turn, ActionType::Bet, 120'000) &&
              has_edge(forbidden, checked_turn, ActionType::AllIn, 440'000),
          "(5) flop checked through: the CO may bet the turn");
  // The same rule on the river: BTN bets that turn, CO calls.
  auto turn_bet_called = flop_checked;
  turn_bet_called.insert(turn_bet_called.end(), {{ActionType::Check, 0},
                                                 {ActionType::Bet, 120'000},
                                                 {ActionType::Call, 120'000}});
  const auto called_river = walk(forbidden, turn_bet_called);
  require(forbidden.nodes()[called_river].street == gtosd::Street::River &&
              check_only(forbidden, called_river),
          "BTN bets the turn, CO calls: the CO may only check the river");

  // Every single-action decision is a lead blocked by a later aggressor.
  for (const auto &node : forbidden.nodes()) {
    if (!check_only(forbidden, node.id)) {
      continue;
    }
    const auto aggressor = previous_round_aggressor(forbidden, node.id);
    require(node.street != gtosd::Street::Preflop && aggressor != pb::no_aggressor &&
                aggressor > node.actor && forbidden.states()[node.id].current_bet.units() == 0,
            "a check-only decision faces no bet and a previous aggressor acting later");
  }
  const auto &allowed_stats = allowed.stats();
  const auto &forbidden_stats = forbidden.stats();
  require(allowed_stats.node_count == 571U && allowed_stats.decision_nodes == 228U &&
              check_only_decisions(allowed) == 0U,
          "HU50 with donk bets: 571 nodes, 228 decisions, none check-only");
  require(forbidden_stats.node_count == 493U && forbidden_stats.decision_nodes == 199U &&
              forbidden_stats.postflop_decisions_by_street ==
                  std::array<std::uint64_t, 3>{25U, 58U, 108U} &&
              check_only_decisions(forbidden) == 11U,
          "HU50 without donk bets: 493 nodes, 199 decisions (25/58/108), 11 check-only");
  require(forbidden.fingerprint() == "fnv1a64:91cdf82d8496ae89",
          "HU50 without donk bets: frozen tree fingerprint, got " + forbidden.fingerprint());

  // compile_subgame does not know the betting before its root: the flop of
  // (1) compiled on its own lets the CO bet, while a BTN bet called after the
  // root restricts the turn as in the full tree.
  const auto subgame =
      pb::CompiledGame::compile_subgame(forbidden_config, forbidden.states()[isolated_flop]);
  require(subgame.has_value(), "the flop subgame compiles");
  const auto &flop_subgame = subgame.value();
  require(has_edge(flop_subgame, flop_subgame.root(), ActionType::Bet, 120'000),
          "an unknown preflop aggressor restricts nothing at the subgame root");
  const auto subgame_turn = walk(flop_subgame, {{ActionType::Check, 0},
                                                {ActionType::Bet, 120'000},
                                                {ActionType::Call, 120'000}});
  require(check_only(flop_subgame, subgame_turn),
          "a flop bet made after the subgame root restricts the turn");

  // The CO40 tree (three sizes, up to four raises a street) under the flag.
  auto co40_config = load_fixture("preflop_blueprint_co40_v1.json");
  co40_config.postflop_donk_bets = false;
  const auto co40 = compile(co40_config);
  test_structure_and_transitions(co40, false);
  test_payoffs(co40);
  require(co40.stats().node_count == 19'510U && co40.stats().decision_nodes == 7'312U,
          "CO40 without donk bets: 19,510 nodes, 7,312 decisions");
  std::cout << "HU50 donk bets: nodes=" << allowed_stats.node_count
            << " decisions=" << allowed_stats.decision_nodes
            << "; no donk bets: nodes=" << forbidden_stats.node_count
            << " decisions=" << forbidden_stats.decision_nodes
            << " check_only=" << check_only_decisions(forbidden)
            << " fingerprint=" << forbidden.fingerprint()
            << "; CO40 no donk bets: nodes=" << co40.stats().node_count << '\n';
}

pb::GameConfig load_monker(const std::string_view name) {
  return load_config(std::filesystem::path("benchmarks") / "monker" / std::string(name));
}

gtosd::PotPercentage five_times_the_pot() {
  return gtosd::PotPercentage::from_basis_points(50'000).value();
}

bool has_all_in(const pb::CompiledGame &game, const std::uint32_t node) {
  for (const auto &edge : game.edges_of(node)) {
    if (edge.action.type == gtosd::ActionType::AllIn) {
      return true;
    }
  }
  return false;
}

// postflop_all_in_max_pot_basis_points: optional integer, serialized only when set.
void test_all_in_cap_config_key() {
  const auto uncapped = load_hu50();
  require(!uncapped.postflop_all_in_max_pot.has_value(),
          "a configuration without the key has no all-in cap");
  require(pb::serialize_game_config_json(uncapped).find("postflop_all_in_max_pot") ==
              std::string::npos,
          "an absent cap is not serialized, so existing fingerprints do not move");

  auto capped = uncapped;
  capped.postflop_all_in_max_pot = five_times_the_pot();
  require(capped != uncapped, "configuration equality sees the cap");
  require(pb::game_config_fingerprint(capped) != pb::game_config_fingerprint(uncapped),
          "the cap enters the configuration fingerprint");
  const auto capped_json = pb::serialize_game_config_json(capped);
  constexpr std::string_view serialized_key = "\"postflop_all_in_max_pot_basis_points\": 50000";
  const auto key_position = capped_json.find(serialized_key);
  require(key_position != std::string::npos, "a set cap is serialized in basis points");
  const auto reparsed = pb::parse_game_config_json(capped_json);
  require(reparsed.has_value() && reparsed.value() == capped,
          "the cap survives a serialization round trip");

  const auto parse_with_value = [&](const std::string_view value) {
    auto text = capped_json;
    text.replace(key_position, serialized_key.size(),
                 std::string("\"postflop_all_in_max_pot_basis_points\": ") + std::string(value));
    return pb::parse_game_config_json(text);
  };
  for (const auto [value, basis_points] :
       {std::pair<std::string_view, std::uint32_t>{"1", 1U},
        std::pair<std::string_view, std::uint32_t>{"100000", 100'000U}}) {
    const auto parsed = parse_with_value(value);
    require(parsed.has_value() && parsed.value().postflop_all_in_max_pot.has_value() &&
                parsed.value().postflop_all_in_max_pot.value().basis_points() == basis_points,
            "caps from one basis point to 10x the pot are accepted");
  }
  for (const std::string_view invalid :
       {"0", "100001", "-1", "50000.0", "5e4", "\"50000\"", "null", "true", "[]"}) {
    const auto parsed = parse_with_value(invalid);
    require(!parsed.has_value() && parsed.error() == pb::ConfigError::InvalidValue,
            "a cap outside 1..100000 or not an integer is rejected as an invalid value");
  }

  // The two benchmark variants are the step 2 trees plus the 5x cap, with
  // their own ids.
  for (const auto [base, variant, id] :
       {std::array<std::string_view, 3>{"HU50_step2.json", "HU50_step2_allin5x.json",
                                        "MONKER-HU50-STEP2-ALLIN5X-001"},
        std::array<std::string_view, 3>{"HU50_step2_donk.json", "HU50_step2_donk_allin5x.json",
                                        "MONKER-HU50-STEP2-DONK-ALLIN5X-001"}}) {
    auto expected = load_monker(base);
    expected.id = std::string(id);
    expected.postflop_all_in_max_pot = five_times_the_pot();
    require(load_monker(variant) == expected,
            std::string(variant) + " is " + std::string(base) + " with the 5x cap");
  }
}

// Without the key every configuration and every tree keeps its fingerprint:
// the suite values gtosd_preflop_blueprint_game recorded in
// benchmarks/suite/actions and the MonkerSolver HU50 trees measured on
// 2026-09-29 before the cap existed.
void test_fingerprints_without_the_cap() {
  struct Frozen {
    std::string_view path;
    std::string_view config;
    std::string_view tree;
  };
  for (const auto &frozen : {Frozen{"benchmarks/suite/fixtures/HU10.json",
                                    "fnv1a64:d7585ed4add81e39", "fnv1a64:d7b31d6f2cb759fc"},
                             Frozen{"benchmarks/suite/fixtures/HU10-FULL.json",
                                    "fnv1a64:d3ea597979f33cbd", "fnv1a64:bc9e7b35ad8c021d"},
                             Frozen{"benchmarks/suite/fixtures/HU20.json",
                                    "fnv1a64:6c7f8a7484f87014", "fnv1a64:f5b432de223744cc"},
                             Frozen{"benchmarks/suite/fixtures/HU20-2.json",
                                    "fnv1a64:daec4717349ea62c", "fnv1a64:7b59c6cacc9f5da5"},
                             Frozen{"benchmarks/suite/fixtures/HU30.json",
                                    "fnv1a64:e89fbd8a269c7494", "fnv1a64:17dc5c7d07ea30c2"},
                             Frozen{"benchmarks/suite/fixtures/HU40.json",
                                    "fnv1a64:abbaffde7936699f", "fnv1a64:18d08f453034ac0f"},
                             Frozen{"benchmarks/suite/fixtures/HU40-FULL.json",
                                    "fnv1a64:51e0b2530dc3511e", "fnv1a64:d6c10723d35b9503"},
                             Frozen{"benchmarks/monker/HU50.json", "fnv1a64:d2ecda83778fd1d2",
                                    "fnv1a64:d535abd7f76a586f"},
                             Frozen{"benchmarks/monker/HU50_step2.json", "fnv1a64:4e975e019c8867e6",
                                    "fnv1a64:61b31653264e1782"},
                             Frozen{"benchmarks/monker/HU50_step2_donk.json",
                                    "fnv1a64:f181a0fec25a83ed", "fnv1a64:c21be92c2a7c9133"}}) {
    const auto config = load_config(std::filesystem::path(frozen.path));
    require(!config.postflop_all_in_max_pot.has_value(),
            std::string(frozen.path) + " carries no all-in cap");
    // The rake keys (2026-09-29) must not move these fingerprints either.
    const auto serialized = pb::serialize_game_config_json(config);
    const auto mode = serialized.find("\"rake_mode\": \"disabled\"");
    require(!config.rake.enabled && mode != std::string::npos &&
                serialized.find("\"rake_") == mode &&
                serialized.find("\"rake_", mode + 1U) == std::string::npos,
            std::string(frozen.path) + " carries no rake and serializes only the rake mode");
    require(pb::game_config_fingerprint(config) == frozen.config,
            std::string(frozen.path) + " keeps its configuration fingerprint, got " +
                pb::game_config_fingerprint(config));
    const auto game = compile(config);
    require(game.fingerprint() == frozen.tree,
            std::string(frozen.path) + " keeps its tree fingerprint, got " + game.fingerprint());
  }
}

// Preflop decisions in node order: public state and actions.
std::vector<std::pair<std::string, std::vector<gtosd::Action>>>
preflop_decisions(const pb::CompiledGame &game) {
  std::vector<std::pair<std::string, std::vector<gtosd::Action>>> result;
  for (const auto &node : game.nodes()) {
    if (node.street != gtosd::Street::Preflop || node.kind != pb::NodeKind::Decision) {
      continue;
    }
    std::vector<gtosd::Action> actions;
    for (const auto &edge : game.edges_of(node.id)) {
      actions.push_back(edge.action);
    }
    result.emplace_back(gtosd::serialize_public_state(game.states()[node.id]), std::move(actions));
  }
  return result;
}

// The rule read off the public state, independently of action_config_at:
// the all-in is offered when the chips it adds above the call are at most
// five times the pot after the call. Counts, by street, the decisions of a
// capped tree that lose it (check-only decisions under the donk-bet rule
// are skipped).
std::array<std::uint64_t, 3> check_all_in_cap_rule(const pb::CompiledGame &game) {
  std::array<std::uint64_t, 3> removed{};
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision || node.street == gtosd::Street::Preflop ||
        check_only(game, node.id)) {
      continue;
    }
    const auto &state = game.states()[node.id];
    const auto to_call = gtosd::amount_to_call(state, state.player_to_act).units();
    const auto stack = state.remaining_stacks[state.player_to_act].units();
    const auto above_call = stack - std::min(stack, to_call);
    const auto pot_after_call = state.pot.units() + to_call;
    const bool within_cap = above_call > 0 && above_call <= 5 * pot_after_call;
    require(has_all_in(game, node.id) == within_cap,
            "a postflop all-in exists exactly when it is at most 5x the pot after the call");
    if (above_call > 0 && !within_cap) {
      ++removed[static_cast<std::size_t>(node.street) - 1U];
    }
  }
  return removed;
}

// Postflop all-in capped at 5x the pot (user decision of 2026-09-29) on the
// MonkerSolver step 2 trees: pots of 4a (limp, check) with 48a behind and
// 12a (open called, or limp, isolation, call) with 44a behind. Amounts are
// the chips the actor adds, in units (1a = 10,000).
void test_postflop_all_in_cap() {
  using gtosd::ActionType;
  const auto uncapped = compile(load_monker("HU50_step2.json"));
  const auto capped_config = load_monker("HU50_step2_allin5x.json");
  const auto capped = compile(capped_config);
  test_structure_and_transitions(capped, false);
  test_payoffs(capped);

  // The preflop is MonkerSolver's tree whatever the cap: same eight
  // decisions, same actions, the 49a open-shove at the root included.
  const auto preflop = preflop_decisions(capped);
  require(preflop.size() == 8U && preflop == preflop_decisions(uncapped) &&
              legacy_preflop_fingerprint(capped) == legacy_preflop_fingerprint(uncapped) &&
              capped.stats().preflop_nodes == uncapped.stats().preflop_nodes &&
              capped.stats().postflop_entries == uncapped.stats().postflop_entries,
          "the cap leaves the preflop part untouched: same 8 decisions and actions");
  require(has_edge(capped, capped.root(), ActionType::AllIn, 490'000),
          "the 49a open-shove stays at the root");

  // (1) Limp, check: pot 4a, 48a behind. The all-in lead (48a > 5 x 4a)
  // disappears for both players; the pot bet stays.
  const std::vector<Step> limp_checked{{ActionType::Call, 10'000}, {ActionType::Check, 0}};
  const auto limped_flop = walk(capped, limp_checked);
  require(capped.nodes()[limped_flop].street == gtosd::Street::Flop &&
              capped.nodes()[limped_flop].actor == 0U &&
              capped.nodes()[limped_flop].action_count == 2U &&
              has_edge(capped, limped_flop, ActionType::Check, 0) &&
              has_edge(capped, limped_flop, ActionType::Bet, 40'000),
          "(1) limped flop: the CO may check or bet 4a, no 48a all-in into 4a");
  require(has_edge(uncapped, walk(uncapped, limp_checked), ActionType::AllIn, 480'000),
          "without the cap the CO may shove 48a into 4a");
  const auto limped_flop_btn = follow(capped, limped_flop, ActionType::Check, 0);
  require(capped.nodes()[limped_flop_btn].action_count == 2U &&
              has_edge(capped, limped_flop_btn, ActionType::Bet, 40'000) &&
              !has_all_in(capped, limped_flop_btn),
          "(1) the BTN after the check: bet 4a, no all-in");

  // (2) Facing the 4a bet: 44a above the call against a pot of 12a after the
  // call (44 <= 60), so the all-in raise (48a added) stays next to the pot
  // raise to 16a.
  const auto facing_bet = follow(capped, limped_flop, ActionType::Bet, 40'000);
  require(capped.nodes()[facing_bet].action_count == 4U &&
              has_edge(capped, facing_bet, ActionType::Fold, 0) &&
              has_edge(capped, facing_bet, ActionType::Call, 40'000) &&
              has_edge(capped, facing_bet, ActionType::Raise, 160'000) &&
              has_edge(capped, facing_bet, ActionType::AllIn, 480'000),
          "(2) facing 4a in the limped pot: fold, call, raise to 16a, all-in 48a");

  // (3) The flop and then the turn checked through: the pot is still 4a, so
  // the turn and river leads lose the all-in too.
  auto flop_checked = limp_checked;
  flop_checked.insert(flop_checked.end(), {{ActionType::Check, 0}, {ActionType::Check, 0}});
  auto turn_checked = flop_checked;
  turn_checked.insert(turn_checked.end(), {{ActionType::Check, 0}, {ActionType::Check, 0}});
  for (const auto &line : {flop_checked, turn_checked}) {
    const auto lead = walk(capped, line);
    const auto after_check = follow(capped, lead, ActionType::Check, 0);
    require(capped.nodes()[lead].street != gtosd::Street::Flop &&
                capped.states()[lead].pot.units() == 40'000 && !has_all_in(capped, lead) &&
                !has_all_in(capped, after_check) &&
                has_edge(capped, lead, ActionType::Bet, 40'000) &&
                has_edge(uncapped, walk(uncapped, line), ActionType::AllIn, 480'000),
            "(3) turn and river of the limped pot checked through: no all-in lead");
  }

  // (4) The 12a pots keep every all-in: 44a <= 5 x 12a.
  const std::vector<Step> open_called{{ActionType::Raise, 50'000}, {ActionType::Call, 40'000}};
  const auto opened_flop = walk(capped, open_called);
  require(has_edge(capped, opened_flop, ActionType::AllIn, 440'000) &&
              has_edge(capped, follow(capped, opened_flop, ActionType::Check, 0), ActionType::AllIn,
                       440'000),
          "(4) open called: both players keep the 44a all-in into 12a");
  const std::vector<Step> limp_isolated_called{
      {ActionType::Call, 10'000}, {ActionType::Bet, 40'000}, {ActionType::Call, 40'000}};
  const auto isolated_flop = walk(capped, limp_isolated_called);
  require(check_only(capped, isolated_flop) &&
              has_edge(capped, follow(capped, isolated_flop, ActionType::Check, 0),
                       ActionType::AllIn, 440'000),
          "(4) limp, isolation, call: the aggressor keeps the 44a all-in into 12a");

  // (5) Boundary: with 22a stacks the limped flop leaves 20a = 5 x 4a behind
  // and the all-in lead stays; one unit more and it goes.
  auto boundary_config = capped_config;
  boundary_config.effective_stack = units(220'000);
  const auto at_boundary = compile(boundary_config);
  require(has_edge(at_boundary, walk(at_boundary, limp_checked), ActionType::AllIn, 200'000),
          "(5) an all-in of exactly 5x the pot is allowed");
  boundary_config.effective_stack = units(220'001);
  const auto above_boundary = compile(boundary_config);
  require(!has_all_in(above_boundary, walk(above_boundary, limp_checked)),
          "(5) one unit above 5x the pot removes it");

  // Everywhere else the tree follows the rule; the removed all-ins are the
  // leads into the 4a pot on each street.
  const auto removed = check_all_in_cap_rule(capped);
  require(check_only_decisions(capped) == check_only_decisions(uncapped),
          "the cap creates no check-only decision");

  // The same cap with donk bets.
  const auto donk = compile(load_monker("HU50_step2_donk.json"));
  const auto donk_capped = compile(load_monker("HU50_step2_donk_allin5x.json"));
  test_structure_and_transitions(donk_capped, false);
  test_payoffs(donk_capped);
  require(preflop_decisions(donk_capped) == preflop, "donk variant: the same preflop");
  const auto donk_removed = check_all_in_cap_rule(donk_capped);
  require(check_only_decisions(donk_capped) == 0U, "donk variant: no check-only decision");

  // Six all-in leads go in each tree (both players, flop, turn and river of
  // the 4a pot), each with the fold-or-call decision behind it: 18 nodes.
  const auto &stats = capped.stats();
  const auto &donk_stats = donk_capped.stats();
  constexpr std::array<std::uint64_t, 3> two_per_street{2U, 2U, 2U};
  require(removed == two_per_street && stats.node_count == 475U && stats.decision_nodes == 193U &&
              stats.postflop_decisions_by_street == std::array<std::uint64_t, 3>{23U, 56U, 106U} &&
              uncapped.stats().node_count == 493U && uncapped.stats().decision_nodes == 199U,
          "HU50 step 2 with the 5x cap: 475 nodes (493 without), 193 decisions (23/56/106)");
  require(capped.fingerprint() == "fnv1a64:d5881c39de9cc238",
          "HU50 step 2 with the 5x cap: frozen tree fingerprint, got " + capped.fingerprint());
  require(donk_removed == two_per_street && donk_stats.node_count == 553U &&
              donk_stats.decision_nodes == 222U &&
              donk_stats.postflop_decisions_by_street ==
                  std::array<std::uint64_t, 3>{26U, 66U, 122U} &&
              donk.stats().node_count == 571U && donk.stats().decision_nodes == 228U,
          "HU50 step 2 donk with the 5x cap: 553 nodes (571 without), 222 decisions (26/66/122)");
  require(donk_capped.fingerprint() == "fnv1a64:2ccd265dfe0e4902",
          "HU50 step 2 donk with the 5x cap: frozen tree fingerprint, got " +
              donk_capped.fingerprint());
  std::cout << "HU50 step 2 all-in <= 5x pot: nodes=" << stats.node_count
            << " decisions=" << stats.decision_nodes
            << " postflop decisions=" << stats.postflop_decisions_by_street[0] << '/'
            << stats.postflop_decisions_by_street[1] << '/' << stats.postflop_decisions_by_street[2]
            << " removed all-ins=" << removed[0] << '/' << removed[1] << '/' << removed[2]
            << " fingerprint=" << capped.fingerprint()
            << " (uncapped nodes=" << uncapped.stats().node_count
            << " decisions=" << uncapped.stats().decision_nodes
            << "); with donk bets: nodes=" << donk_stats.node_count
            << " decisions=" << donk_stats.decision_nodes
            << " postflop decisions=" << donk_stats.postflop_decisions_by_street[0] << '/'
            << donk_stats.postflop_decisions_by_street[1] << '/'
            << donk_stats.postflop_decisions_by_street[2] << " removed all-ins=" << donk_removed[0]
            << '/' << donk_removed[1] << '/' << donk_removed[2]
            << " fingerprint=" << donk_capped.fingerprint()
            << " (uncapped nodes=" << donk.stats().node_count
            << " decisions=" << donk.stats().decision_nodes << ")\n";
}

// Rake of the MonkerSolver charts (user, 2026-09-29): 5 %, cap 3 antes, no
// flop no drop.
gtosd::RakeConfig monker_rake() {
  gtosd::RakeConfig rake;
  rake.enabled = true;
  rake.percentage = gtosd::RangeWeight::from_basis_points(500).value();
  rake.cap = antes(3);
  rake.no_flop_no_drop = true;
  rake.minimum_pot = units(0);
  return rake;
}

// rake_mode "enabled" plus four parameters, serialized only when enabled. The
// two MonkerSolver variants are their base trees plus the rake, with their
// own ids.
void test_rake_config_key() {
  for (const auto [base, variant, id] :
       {std::array<std::string_view, 3>{"HU50.json", "HU50_rake.json",
                                        "MONKER-HU50-SYMMETRICAL-RAKE-001"},
        std::array<std::string_view, 3>{"HU50_step2_donk.json", "HU50_step2_donk_rake.json",
                                        "MONKER-HU50-STEP2-DONK-RAKE-001"}}) {
    const auto without = load_monker(base);
    require(!without.rake.enabled &&
                pb::serialize_game_config_json(without).find("rake_basis_points") ==
                    std::string::npos,
            std::string(base) + " has no rake and serializes no rake parameter");
    auto expected = without;
    expected.id = std::string(id);
    expected.rake = monker_rake();
    const auto raked = load_monker(variant);
    require(raked == expected, std::string(variant) + " is " + std::string(base) +
                                   " with 5 %, cap 3 antes, no flop no drop");
    auto renamed = without;
    renamed.id = std::string(id);
    require(raked != renamed &&
                pb::game_config_fingerprint(raked) != pb::game_config_fingerprint(renamed),
            "the rake enters operator== and the configuration fingerprint");
    const auto reparsed = pb::parse_game_config_json(pb::serialize_game_config_json(raked));
    require(reparsed.has_value() && reparsed.value() == raked &&
                pb::game_config_fingerprint(reparsed.value()) == pb::game_config_fingerprint(raked),
            "the rake survives a serialization round trip");
  }
}

// Rake of a terminal recomputed with plain integer arithmetic, independently
// of calculate_rake: the percentage of the called pot rounded half up, at most
// the cap; nothing below the minimum pot or, under no flop no drop, when the
// hand ended before the flop.
std::int64_t expected_rake(const gtosd::RakeConfig &rake, const std::int64_t pot,
                           const bool flop_dealt) {
  if (!rake.enabled || pot < rake.minimum_pot.units() || (rake.no_flop_no_drop && !flop_dealt)) {
    return 0;
  }
  return std::min<std::int64_t>(
      (pot * static_cast<std::int64_t>(rake.percentage.basis_points()) + 5'000) / 10'000,
      rake.cap.units());
}

struct RakeCensus {
  std::uint64_t preflop_folds{0U};
  std::uint64_t preflop_showdowns{0U};
  std::uint64_t postflop_folds{0U};
  std::uint64_t postflop_showdowns{0U};
  std::uint64_t postflop_all_ins{0U};
  // Terminals by rake: 0 with the flop dealt (minimum pot), raked below the
  // cap, raked at a binding cap.
  std::uint64_t flop_unraked{0U};
  std::uint64_t below_cap{0U};
  std::uint64_t cap_binding{0U};
  std::int64_t preflop_fold_rake{0};
  std::int64_t postflop_fold_minimum_rake{0};
  std::uint64_t odd_ties{0U};
};

// Every terminal of a heads-up game with equal stacks: its payoffs (every
// winner subset) sum to minus the recomputed rake, and a tie gives both seats
// half of the raked pot, the odd unit to seat 0 (split_pot).
RakeCensus check_raked_payoffs(const pb::CompiledGame &game) {
  const auto &rake = game.config().rake;
  RakeCensus census;
  census.postflop_fold_minimum_rake = std::numeric_limits<std::int64_t>::max();
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    const auto pot = game.states()[node.id].pot.units();
    const bool preflop = node.street == gtosd::Street::Preflop;
    // A preflop showdown is a called all-in (or a checkdown leaf): the board
    // is dealt, so only a preflop fold ends the hand before the flop.
    const bool flop_dealt = !preflop || node.kind == pb::NodeKind::TerminalShowdown;
    const auto raked = expected_rake(rake, pot, flop_dealt);
    const auto uncapped =
        (pot * static_cast<std::int64_t>(rake.percentage.basis_points()) + 5'000) / 10'000;
    if (flop_dealt && raked == 0) {
      ++census.flop_unraked;
    } else if (raked > 0 && uncapped > rake.cap.units()) {
      ++census.cap_binding;
    } else if (raked > 0) {
      ++census.below_cap;
    }
    if (node.kind == pb::NodeKind::TerminalFold) {
      const auto payoffs = game.fold_payoffs(node.id);
      require(payoffs[0] + payoffs[1] == -raked, "a fold pays the recomputed rake");
      if (preflop) {
        ++census.preflop_folds;
        census.preflop_fold_rake = std::max(census.preflop_fold_rake, raked);
      } else {
        ++census.postflop_folds;
        census.postflop_fold_minimum_rake = std::min(census.postflop_fold_minimum_rake, raked);
      }
      continue;
    }
    if (preflop) {
      ++census.preflop_showdowns;
    } else if (node.remaining_board_cards == 0U) {
      ++census.postflop_showdowns;
    } else {
      ++census.postflop_all_ins;
    }
    for (const std::uint8_t winners : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
      const auto payoffs = game.showdown_payoffs(node.id, winners);
      require(payoffs[0] + payoffs[1] == -raked, "a showdown pays the recomputed rake");
    }
    const auto tie = game.showdown_payoffs(node.id, std::uint8_t{3});
    const auto odd = (pot - raked) % 2;
    require(tie[0] - tie[1] == odd, "a tie splits the raked pot, the odd unit to seat 0");
    census.odd_ties += static_cast<std::uint64_t>(odd);
  }
  return census;
}

// Raked payoffs of every terminal kind against core settle_terminal
// (test_payoffs) and against the rake recomputed above.
void test_raked_payoffs() {
  // HU50 step 2: pots from 4a (limp, check) to 100a (all-in: the antes come
  // out of the 50a stacks); 5 % reaches the 3a cap at 60a.
  const auto hu50 = compile(load_monker("HU50_step2_donk_rake.json"));
  test_payoffs(hu50);
  const auto census = check_raked_payoffs(hu50);
  require(census.preflop_folds > 0U && census.preflop_fold_rake == 0,
          "HU50: a preflop fold is not raked (no flop, no drop)");
  require(census.preflop_showdowns > 0U, "HU50: preflop all-ins exist");
  require(census.postflop_folds > 0U && census.postflop_fold_minimum_rake > 0,
          "HU50: every postflop fold is raked");
  require(census.postflop_showdowns > 0U && census.postflop_all_ins > 0U,
          "HU50: river showdowns and postflop all-in runouts exist");
  require(census.cap_binding > 0U && census.below_cap > 0U && census.flop_unraked == 0U,
          "HU50: the cap binds on large pots only, and every pot after the flop is raked");
  // The preflop all-in: 100a, 5 % = 5a capped at 3a; the winner nets
  // 100 - 3 - 50 = 47 antes, a tie 48.5 - 50 = -1.5 antes each.
  bool all_in_seen = false;
  for (const auto &node : hu50.nodes()) {
    if (node.kind == pb::NodeKind::TerminalShowdown && node.street == gtosd::Street::Preflop) {
      require(hu50.states()[node.id].pot == antes(100), "HU50: the preflop all-in pot is 100a");
      require(hu50.showdown_payoffs(node.id, std::uint8_t{1})[0] == 470'000 &&
                  hu50.showdown_payoffs(node.id, std::uint8_t{1})[1] == -500'000 &&
                  hu50.showdown_payoffs(node.id, std::uint8_t{3})[0] == -15'000 &&
                  hu50.showdown_payoffs(node.id, std::uint8_t{3})[1] == -15'000,
              "HU50: the raked all-in pays 47a to the winner, -1.5a each on a tie");
      all_in_seen = true;
    }
  }
  require(all_in_seen, "HU50: the preflop all-in was checked");

  // HU10 reduced with a 0.5 ante cap: binding at 12a (open, call) and 20a
  // (all-in), not at 4a (limp, check) or 9.28a (a 66 % bet called in it).
  const auto hu10_config = load_fixture("preflop_blueprint_hu10_reduced_rake_v1.json");
  const auto hu10 = compile(hu10_config);
  test_payoffs(hu10);
  const auto hu10_census = check_raked_payoffs(hu10);
  require(hu10_census.preflop_fold_rake == 0 && hu10_census.postflop_fold_minimum_rake > 0 &&
              hu10_census.cap_binding > 0U && hu10_census.below_cap > 0U &&
              hu10_census.postflop_all_ins > 0U && hu10_census.preflop_showdowns > 0U,
          "HU10 rake: every terminal kind, the cap binding and not");
  // Without no flop no drop the preflop folds pay too: the root fold of CO
  // leaves a called pot of 2a (the dead antes; the uncalled button blind goes
  // back to the BTN), 5 % = 0.1a.
  auto dropped = hu10_config;
  dropped.rake.no_flop_no_drop = false;
  const auto dropped_game = compile(dropped);
  test_payoffs(dropped_game);
  const auto dropped_census = check_raked_payoffs(dropped_game);
  require(dropped_census.preflop_fold_rake > 0, "without no flop no drop a preflop fold is raked");
  const auto root_fold = follow(dropped_game, dropped_game.root(), gtosd::ActionType::Fold, 0);
  const auto root_payoffs = dropped_game.fold_payoffs(root_fold);
  require(dropped_game.states()[root_fold].pot == antes(2) &&
              root_payoffs[0] + root_payoffs[1] == -1'000 && root_payoffs[0] == -10'000 &&
              root_payoffs[1] == 9'000,
          "the root fold pays 5 % of the 2a called pot when no flop no drop is off, pot " +
              std::to_string(dropped_game.states()[root_fold].pot.units()));
  // A 10a minimum pot: the limped pots (4a, 9.28a) are not raked, 12a is.
  auto minimum = hu10_config;
  minimum.rake.minimum_pot = antes(10);
  const auto minimum_game = compile(minimum);
  test_payoffs(minimum_game);
  const auto minimum_census = check_raked_payoffs(minimum_game);
  require(minimum_census.flop_unraked > 0U && minimum_census.cap_binding > 0U,
          "a minimum pot leaves the small pots unraked and rakes the others");
  std::cout << "raked payoffs: HU50 step 2 donk rake " << census.preflop_folds << " preflop folds, "
            << census.preflop_showdowns << " preflop all-ins, " << census.postflop_folds
            << " postflop folds, " << census.postflop_showdowns << " river showdowns, "
            << census.postflop_all_ins << " postflop all-ins, cap binding " << census.cap_binding
            << ", below the cap " << census.below_cap << ", odd ties " << census.odd_ties
            << "; HU10 rake cap binding " << hu10_census.cap_binding << ", below the cap "
            << hu10_census.below_cap << ", odd ties " << hu10_census.odd_ties
            << "; minimum pot 10a unraked " << minimum_census.flop_unraked << '\n';
}

// Checkdown leaves (CompileOptions::checkdown_at_flop): the stored state stays
// StreetComplete on the preflop street, but the leaf settles as a river
// showdown, so it is raked under no flop no drop. HU50: limp, check 4a
// -> 0.2a; open, call 12a -> 0.6a; the all-in 100a -> 3a (cap).
void test_checkdown_rake() {
  pb::CompileOptions options;
  options.checkdown_at_flop = true;
  const auto plain = compile(load_hu50(), options);
  const auto raked = compile(load_monker("HU50_rake.json"), options);
  // Frozen by the step-1 checkdown runs (out/monker/step1, 2026-09-28).
  require(plain.fingerprint() == "fnv1a64:5d3506ee3b821433",
          "the HU50 checkdown tree keeps its fingerprint, got " + plain.fingerprint());
  require(plain.nodes().size() == raked.nodes().size() &&
              raked.fingerprint() == "fnv1a64:10ff774cde702ed9",
          "the rake does not change the tree; its fingerprint (config included) is frozen");
  std::map<std::int64_t, std::uint64_t> leaf_rakes;
  std::map<std::int64_t, std::uint64_t> all_in_rakes;
  for (const auto &node : raked.nodes()) {
    const auto &state = raked.states()[node.id];
    if (node.kind == pb::NodeKind::TerminalFold) {
      const auto settlement = gtosd::settle_terminal(state, raked.config().rake);
      const auto payoffs = raked.fold_payoffs(node.id);
      require(settlement.has_value() && payoffs[0] == settlement.value().payoff_units[0] &&
                  payoffs[1] == settlement.value().payoff_units[1] &&
                  payoffs[0] + payoffs[1] == 0,
              "a preflop fold of the checkdown tree is not raked (no flop, no drop)");
      continue;
    }
    if (node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    require(node.street == gtosd::Street::Preflop && node.remaining_board_cards == 5U,
            "every showdown of the checkdown tree is preflop with five cards to come");
    const bool leaf = state.status == gtosd::HandStatus::StreetComplete;
    auto settled = state;
    if (leaf) {
      settled.status = gtosd::HandStatus::Showdown;
      settled.street = gtosd::Street::River;
    }
    for (const std::uint8_t winners : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
      const auto settlement = gtosd::settle_terminal(settled, raked.config().rake, winners);
      require(settlement.has_value(), "a checkdown showdown settles");
      const auto payoffs = raked.showdown_payoffs(node.id, winners);
      require(payoffs[0] == settlement.value().payoff_units[0] &&
                  payoffs[1] == settlement.value().payoff_units[1] &&
                  payoffs[0] + payoffs[1] == -settlement.value().rake.units(),
              "a checkdown showdown pays settle_terminal as a river showdown");
      const auto unraked = plain.showdown_payoffs(node.id, winners);
      require(unraked[0] + unraked[1] == 0, "without rake the checkdown game is zero-sum");
    }
    const auto rake = -(raked.showdown_payoffs(node.id, std::uint8_t{1})[0] +
                        raked.showdown_payoffs(node.id, std::uint8_t{1})[1]);
    ++(leaf ? leaf_rakes : all_in_rakes)[rake];
  }
  require(leaf_rakes.size() == 2U && leaf_rakes.contains(2'000) && leaf_rakes.contains(6'000),
          "checkdown leaves pay 0.2a (limped pot) and 0.6a (open called)");
  require(all_in_rakes.size() == 1U && all_in_rakes.contains(30'000),
          "the preflop all-in pays the 3a cap");
  std::cout << "checkdown rake: " << leaf_rakes.at(2'000) << " leaves at 0.2a, "
            << leaf_rakes.at(6'000) << " at 0.6a, " << all_in_rakes.at(30'000)
            << " all-ins at 3a, tree " << raked.fingerprint() << '\n';
}

// The rake fixtures, frozen when they were written (2026-09-29).
void test_rake_fingerprints() {
  struct Frozen {
    std::string_view path;
    std::string_view config;
    std::string_view tree;
  };
  for (const auto &frozen :
       {Frozen{"benchmarks/monker/HU50_rake.json", "fnv1a64:0c39632446522b06",
               "fnv1a64:701e82eadbce13d5"},
        Frozen{"benchmarks/monker/HU50_step2_donk_rake.json", "fnv1a64:fa9fa010c59f1589",
               "fnv1a64:b7be1d863a69cf84"},
        Frozen{"benchmarks/fixtures/preflop_blueprint_hu10_reduced_rake_v1.json",
               "fnv1a64:7074b0ccaf262fc3", "fnv1a64:7c80027ef6f00600"}}) {
    const auto config = load_config(std::filesystem::path(frozen.path));
    const auto game = compile(config);
    require(pb::game_config_fingerprint(config) == frozen.config,
            std::string(frozen.path) + " keeps its configuration fingerprint, got " +
                pb::game_config_fingerprint(config));
    require(game.fingerprint() == frozen.tree,
            std::string(frozen.path) + " keeps its tree fingerprint, got " + game.fingerprint());
  }
}

// Rake in the 3-way payoffs (3WAY50_donk_rake: 5 %, cap 3 antes, no flop no
// drop): every terminal of the full tree, at every winner subset of its active
// players, sums to minus the rake recomputed from its pot; the 25 preflop
// folds pay none, and the 36 preflop all-ins (called pots of 101a to 150a,
// flop dealt) pay the 3a cap.
void test_three_way_rake() {
  const auto game = compile(load_monker("3WAY50_donk_rake.json"));
  const auto &rake = game.config().rake;
  require(rake.enabled && rake.cap == antes(3) && rake.percentage.basis_points() == 500U &&
              rake.no_flop_no_drop,
          "3WAY50_donk_rake: 5 %, cap 3 antes, no flop no drop");
  test_payoffs(game);
  std::uint64_t preflop_folds = 0U;
  std::uint64_t preflop_all_ins = 0U;
  std::uint64_t postflop_raked = 0U;
  const auto players = game.config().player_count;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    const auto pot = game.states()[node.id].pot.units();
    const bool preflop = node.street == gtosd::Street::Preflop;
    const bool flop_dealt = !preflop || node.kind == pb::NodeKind::TerminalShowdown;
    const auto raked = expected_rake(rake, pot, flop_dealt);
    const auto sum = [&](const std::span<const std::int64_t> payoffs) {
      std::int64_t total = 0;
      for (std::uint8_t player = 0; player < players; ++player) {
        total += payoffs[player];
      }
      return total;
    };
    if (node.kind == pb::NodeKind::TerminalFold) {
      require(sum(game.fold_payoffs(node.id)) == -raked, "3-way: a fold pays the recomputed rake");
      preflop_folds += preflop ? 1U : 0U;
      require(!preflop || raked == 0, "3-way: a preflop fold is not raked");
    } else {
      for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
        if ((winners & static_cast<std::uint8_t>(~node.active_mask)) != 0U) {
          continue;
        }
        require(sum(game.showdown_payoffs(node.id, winners)) == -raked,
                "3-way: every winner subset pays the recomputed rake");
      }
      if (preflop) {
        ++preflop_all_ins;
        require(raked == 30'000 && pot >= 1'010'000,
                "3-way: a preflop all-in (101a to 150a) pays the 3a cap");
      }
    }
    postflop_raked += (!preflop && raked > 0) ? 1U : 0U;
  }
  require(preflop_folds == 25U && preflop_all_ins == 36U && postflop_raked > 0U,
          "3-way rake: 25 unraked preflop folds, 36 capped preflop all-ins, raked postflop");
  std::cout << "3-way rake: preflop folds " << preflop_folds << ", preflop all-ins "
            << preflop_all_ins << ", raked postflop terminals " << postflop_raked << '\n';
}

// postflop_betting_streets: optional list of the postflop streets with
// betting, serialized only when a street does not bet.
void test_betting_streets_config_key() {
  const auto all = load_hu50();
  require(all.postflop_betting_streets.all(),
          "a configuration without the key bets on every postflop street");
  require(pb::serialize_game_config_json(all).find("postflop_betting_streets") ==
              std::string::npos,
          "betting on every street is not serialized, so existing fingerprints do not move");

  auto flop_turn = all;
  flop_turn.postflop_betting_streets.river = false;
  require(flop_turn != all, "configuration equality sees the betting streets");
  require(pb::game_config_fingerprint(flop_turn) != pb::game_config_fingerprint(all),
          "the betting streets enter the configuration fingerprint");
  const auto flop_turn_json = pb::serialize_game_config_json(flop_turn);
  const auto key_position = flop_turn_json.find("\"postflop_betting_streets\": [");
  require(key_position != std::string::npos, "a street without betting is serialized");
  const auto key_end = flop_turn_json.find(']', key_position);
  require(key_end != std::string::npos, "the betting streets are a list");
  const auto reparsed = pb::parse_game_config_json(flop_turn_json);
  require(reparsed.has_value() && reparsed.value() == flop_turn,
          "the betting streets survive a serialization round trip");

  const auto parse_with_value = [&](const std::string_view value) {
    auto text = flop_turn_json;
    text.replace(key_position, key_end + 1U - key_position,
                 std::string("\"postflop_betting_streets\": ") + std::string(value));
    return pb::parse_game_config_json(text);
  };
  const auto explicit_all = parse_with_value(R"(["flop", "turn", "river"])");
  require(explicit_all.has_value() && explicit_all.value() == all &&
              pb::game_config_fingerprint(explicit_all.value()) ==
                  pb::game_config_fingerprint(all),
          "an explicit list of the three streets is the default and keeps the fingerprint");
  const auto river_only = parse_with_value(R"(["river"])");
  require(river_only.has_value() && !river_only.value().postflop_betting_streets.flop &&
              !river_only.value().postflop_betting_streets.turn &&
              river_only.value().postflop_betting_streets.river,
          "a river-only list parses");
  for (const std::string_view invalid :
       {std::string_view("[]"), std::string_view(R"(["turn", "flop"])"),
        std::string_view(R"(["flop", "flop"])"), std::string_view(R"("flop")"),
        std::string_view("null")}) {
    const auto parsed = parse_with_value(invalid);
    require(!parsed.has_value() && parsed.error() == pb::ConfigError::InvalidStructure,
            "an empty, unordered, repeated or non-list postflop_betting_streets is rejected as "
            "an invalid structure: " +
                std::string(invalid));
  }
  for (const std::string_view invalid : {std::string_view(R"(["preflop"])"),
                                         std::string_view(R"(["Flop"])"), std::string_view("[1]")}) {
    const auto parsed = parse_with_value(invalid);
    require(!parsed.has_value() && parsed.error() == pb::ConfigError::InvalidValue,
            "an unknown street name is rejected as an invalid value: " + std::string(invalid));
  }
  auto none = all;
  none.postflop_betting_streets = pb::PostflopBettingStreets{false, false, false};
  const auto validated = pb::validate_game_config(none);
  require(!validated.has_value() && validated.error() == pb::ConfigError::InvalidStructure,
          "a configuration without any betting street is invalid");
  require(!pb::CompiledGame::compile(none).has_value(),
          "a configuration without any betting street does not compile");
}

// The small games of the correctness tests (benchmarks/monker/correctness,
// 30 September 2026): HU, 6a stacks, the all-in as the only preflop raise and
// the only postflop bet. The betting streets change only the postflop: every
// decision on a street without betting is check-only, no decision on a
// betting street is (donk bets allowed), and the preflop part is that of the
// all-street game. Counts and fingerprints frozen for the correctness runs.
void test_betting_streets_trees() {
  struct Expected {
    std::string_view path;
    std::string_view config;
    std::string_view tree;
    std::uint32_t nodes;
    std::array<std::uint32_t, 4> decisions;
    std::array<bool, 3> betting;
  };
  const auto directory = std::filesystem::path("benchmarks") / "monker" / "correctness";
  const auto all = compile(load_config(directory / "HU6_all.json"));
  for (const auto &expected : {
           Expected{"HU6_all.json", "fnv1a64:d6c6d57c0522a56a", "fnv1a64:fb76ddcd880fec5f", 37U, {4U, 4U, 4U, 4U},
                    {true, true, true}},
           Expected{"HU6_all_rake25cap2.json", "fnv1a64:8daa713dbe98e149", "fnv1a64:f226b87d43f28215", 37U, {4U, 4U, 4U, 4U},
                    {true, true, true}},
           Expected{"HU6_V0_flop.json", "fnv1a64:73fc810af0c52da4", "fnv1a64:cd66796bdbdac5c1", 25U, {4U, 4U, 2U, 2U},
                    {true, false, false}},
           Expected{"HU6_V1_flopturn.json", "fnv1a64:ddbc481fbc3f6d4b", "fnv1a64:da5c6942354ad5ad", 31U, {4U, 4U, 4U, 2U},
                    {true, true, false}},
           Expected{"HU6_V2_river.json", "fnv1a64:65c8bbbcf1d4df6c", "fnv1a64:2f109f6f1891d9f2", 25U, {4U, 2U, 2U, 4U},
                    {false, false, true}},
       }) {
    const auto config = load_config(directory / std::string(expected.path));
    const auto game = compile(config);
    test_structure_and_transitions(game, false);
    test_payoffs(game);
    require(legacy_preflop_fingerprint(game) == legacy_preflop_fingerprint(all) ||
                config.rake.enabled,
            std::string(expected.path) + ": the preflop part is that of the all-street game");
    std::array<std::uint32_t, 4> decisions{};
    std::array<std::uint32_t, 4> check_only_count{};
    for (const auto &node : game.nodes()) {
      if (node.kind != pb::NodeKind::Decision) {
        continue;
      }
      const auto street = static_cast<std::size_t>(node.street);
      ++decisions[street];
      check_only_count[street] += check_only(game, node.id) ? 1U : 0U;
    }
    require(check_only_count[0] == 0U, std::string(expected.path) + ": no check-only preflop");
    for (std::size_t street = 1U; street < 4U; ++street) {
      require(expected.betting[street - 1U]
                  ? check_only_count[street] == 0U
                  : check_only_count[street] == decisions[street],
              std::string(expected.path) + ": check-only decisions exactly on the streets "
                                           "without betting");
      require(config.postflop_betting_streets.bets_on(static_cast<gtosd::Street>(street)) ==
                  expected.betting[street - 1U],
              std::string(expected.path) + ": betting streets of the configuration");
    }
    std::cout << expected.path << ": config=" << pb::game_config_fingerprint(config)
              << " tree=" << game.fingerprint() << " nodes=" << game.stats().node_count
              << " decisions=" << decisions[0] << '/' << decisions[1] << '/' << decisions[2]
              << '/' << decisions[3] << " check-only=" << check_only_count[1] << '/'
              << check_only_count[2] << '/' << check_only_count[3] << '\n';
    require(decisions == expected.decisions,
            std::string(expected.path) + ": decisions per street");
    require(game.stats().node_count == expected.nodes,
            std::string(expected.path) + ": node count");
    require(pb::game_config_fingerprint(config) == expected.config,
            std::string(expected.path) + ": configuration fingerprint");
    require(game.fingerprint() == expected.tree, std::string(expected.path) + ": tree fingerprint");
  }
}

// Rake of the raked correctness runs (rake study R1, 30 September 2026):
// 2.5 %, cap 2 antes, no flop no drop, no minimum pot.
gtosd::RakeConfig correctness_rake() {
  gtosd::RakeConfig rake;
  rake.enabled = true;
  rake.percentage = gtosd::RangeWeight::from_basis_points(250).value();
  rake.cap = antes(2);
  rake.no_flop_no_drop = true;
  rake.minimum_pot = units(0);
  return rake;
}

// Two compilations of one tree under a different rake: every node, edge,
// public state and postflop entry is identical; only the payoffs and the
// fingerprint (which includes the configuration) may differ.
void require_same_tree(const pb::CompiledGame &left, const pb::CompiledGame &right,
                       const std::string &label) {
  require(left.nodes().size() == right.nodes().size() &&
              left.edges().size() == right.edges().size() &&
              left.states().size() == right.states().size() &&
              left.postflop_entries() == right.postflop_entries(),
          label + ": same node, edge and state counts and the same postflop entries");
  for (std::size_t index = 0; index < left.nodes().size(); ++index) {
    const auto &a = left.nodes()[index];
    const auto &b = right.nodes()[index];
    require(a.id == b.id && a.parent == b.parent && a.subtree_end == b.subtree_end &&
                a.first_edge == b.first_edge && a.payoff_offset == b.payoff_offset &&
                a.postflop_entry == b.postflop_entry && a.depth == b.depth && a.kind == b.kind &&
                a.street == b.street && a.actor == b.actor && a.action_count == b.action_count &&
                a.active_mask == b.active_mask &&
                a.remaining_board_cards == b.remaining_board_cards && a.level == b.level &&
                a.limped_pot == b.limped_pot,
            label + ": node " + std::to_string(index) + " is identical");
    require(left.states()[index] == right.states()[index],
            label + ": public state " + std::to_string(index) + " is identical");
  }
  for (std::size_t index = 0; index < left.edges().size(); ++index) {
    require(left.edges()[index].action == right.edges()[index].action &&
                left.edges()[index].child == right.edges()[index].child,
            label + ": edge " + std::to_string(index) + " is identical");
  }
}

// Every payoff of two node-identical trees: every seat at every fold, every
// winner subset at every showdown.
bool same_payoffs(const pb::CompiledGame &left, const pb::CompiledGame &right) {
  const auto players = left.config().player_count;
  const auto equal = [players](const std::span<const std::int64_t> first,
                               const std::span<const std::int64_t> second) {
    for (std::uint8_t player = 0; player < players; ++player) {
      if (first[player] != second[player]) {
        return false;
      }
    }
    return true;
  };
  for (const auto &node : left.nodes()) {
    if (node.kind == pb::NodeKind::TerminalFold &&
        !equal(left.fold_payoffs(node.id), right.fold_payoffs(node.id))) {
      return false;
    }
    if (node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
      if ((winners & static_cast<std::uint8_t>(~node.active_mask)) != 0U) {
        continue;
      }
      if (!equal(left.showdown_payoffs(node.id, winners),
                 right.showdown_payoffs(node.id, winners))) {
        return false;
      }
    }
  }
  return true;
}

// The HU6 correctness games by hand, from the rules of the game and not from
// settle_terminal. Stacks 6a, of which the 1a ante is dead; BTN posts a live
// 1a blind, so the pot is 3a with 5a (CO) and 4a (BTN) behind, and the only
// preflop raise, like the only postflop bet, is the all-in. CO acts first on
// every postflop street. The called pot is what both players put in once an
// uncalled excess has gone back:
//   CO folds at the root: 2a, the antes (BTN's blind comes back); BTN +1a,
//     CO -1a.
//   A fold facing an all-in: 4a, the antes, the blind and the limp or the
//     blind matched (the excess comes back); +2a / -2a.
//   An all-in called: 12a, both stacks; +6a / -6a, a tie 0 each.
//   The river checked down: 4a; +2a / -2a, a tie 0 each.
// Rake 2.5 %, cap 2a, no flop no drop: nothing on the three preflop folds
// (no flop), 0.1a on a 4a pot after the flop, 0.3a on a 12a pot (a called
// preflop all-in deals the board, so it is raked). The winners pay it:
// 4a -> +1.9a / -2a, a tie -0.05a each; 12a -> +5.7a / -6a, a tie -0.15a
// each. The cap never binds, nothing rounds and no tie has an odd unit.

// Net result in units of a sole winner, of the loser, and of each seat on a
// tie (showdowns only).
struct HandNet {
  std::int64_t winner{0};
  std::int64_t loser{0};
  std::int64_t tie{0};
};

struct HandDerivedTerminal {
  // Actions from the root: f fold, x check, c call (an all-in call
  // included), a the all-in (bet or raise), / a street transition.
  std::string path;
  gtosd::Street street{gtosd::Street::Preflop};
  // Seat mask of the player who wins a fold (CO 1, BTN 2); 0 at a showdown.
  std::uint8_t fold_winner{0U};
  // Board cards still to come at a showdown.
  std::uint8_t remaining_board_cards{0U};
  std::int64_t pot{0};
  std::int64_t rake{0};
  HandNet unraked;
  HandNet raked;
};

// The 18 terminals of HU6_all (bets on every street) or the 10 of
// HU6_V2_river (check-only flop and turn).
std::vector<HandDerivedTerminal> hu6_hand_derived_terminals(const bool bets_before_river) {
  constexpr HandNet fold_2a{10'000, -10'000, 0};
  constexpr HandNet unraked_4a{20'000, -20'000, 0};
  constexpr HandNet raked_4a{19'000, -20'000, -500};
  constexpr HandNet unraked_12a{60'000, -60'000, 0};
  constexpr HandNet raked_12a{57'000, -60'000, -1'500};
  std::vector<HandDerivedTerminal> terminals = {
      {"f", gtosd::Street::Preflop, 0b10U, 0U, 20'000, 0, fold_2a, fold_2a},
      {"caf", gtosd::Street::Preflop, 0b10U, 0U, 40'000, 0, unraked_4a, unraked_4a},
      {"af", gtosd::Street::Preflop, 0b01U, 0U, 40'000, 0, unraked_4a, unraked_4a},
      {"ac", gtosd::Street::Preflop, 0U, 5U, 120'000, 3'000, unraked_12a, raked_12a},
      {"cac", gtosd::Street::Preflop, 0U, 5U, 120'000, 3'000, unraked_12a, raked_12a}};
  std::string prefix = "cx/";
  for (const auto street : {gtosd::Street::Flop, gtosd::Street::Turn, gtosd::Street::River}) {
    if (bets_before_river || street == gtosd::Street::River) {
      const auto remaining = static_cast<std::uint8_t>(street == gtosd::Street::Flop   ? 2U
                                                       : street == gtosd::Street::Turn ? 1U
                                                                                       : 0U);
      terminals.push_back({prefix + "xaf", street, 0b10U, 0U, 40'000, 1'000, unraked_4a, raked_4a});
      terminals.push_back({prefix + "af", street, 0b01U, 0U, 40'000, 1'000, unraked_4a, raked_4a});
      terminals.push_back(
          {prefix + "xac", street, 0U, remaining, 120'000, 3'000, unraked_12a, raked_12a});
      terminals.push_back(
          {prefix + "ac", street, 0U, remaining, 120'000, 3'000, unraked_12a, raked_12a});
    }
    prefix += street == gtosd::Street::River ? "xx" : "xx/";
  }
  terminals.push_back({prefix, gtosd::Street::River, 0U, 0U, 40'000, 1'000, unraked_4a, raked_4a});
  return terminals;
}

std::int64_t hand_net(const HandNet &net, const std::uint8_t winners, const std::uint8_t player) {
  if (winners == 0b11U) {
    return net.tie;
  }
  return ((winners >> player) & 1U) != 0U ? net.winner : net.loser;
}

char action_symbol(const gtosd::Action &action) {
  if (pb::is_aggressive(action)) {
    return 'a';
  }
  if (action.type == gtosd::ActionType::Fold) {
    return 'f';
  }
  if (action.type == gtosd::ActionType::Check) {
    return 'x';
  }
  return action.type == gtosd::ActionType::Call ? 'c' : '?';
}

// Terminals of a compiled tree by their line of actions (the symbols of
// HandDerivedTerminal::path). Every aggressive action must put the actor
// all-in: in the HU6 games the all-in is the only raise and the only bet.
void collect_terminal_paths(const pb::CompiledGame &game, const std::uint32_t node,
                            const std::string &path,
                            std::map<std::string, std::uint32_t> &terminals) {
  const auto &compiled = game.nodes()[node];
  if (compiled.kind == pb::NodeKind::TerminalFold ||
      compiled.kind == pb::NodeKind::TerminalShowdown) {
    require(terminals.emplace(path, node).second, "terminal lines are unique: " + path);
    return;
  }
  if (compiled.kind == pb::NodeKind::Chance) {
    collect_terminal_paths(game, game.edges_of(node)[0].child, path + "/", terminals);
    return;
  }
  for (const auto &edge : game.edges_of(node)) {
    const auto symbol = action_symbol(edge.action);
    if (symbol == 'a') {
      require(game.states()[edge.child].remaining_stacks[compiled.actor].units() == 0,
              "the only raise and the only bet is the all-in: " + path + symbol);
    }
    collect_terminal_paths(game, edge.child, path + symbol, terminals);
  }
}

// U1: every terminal of an HU6 pair (the same tree without and with the
// correctness rake) has the hand-derived payoffs above, and the rake moves
// only the winners: raked - unraked = -rake at a sole winner, -rake / 2 at
// each seat of a tie, 0 at the loser.
void check_hand_derived_terminals(const pb::CompiledGame &plain, const pb::CompiledGame &raked,
                                  const bool bets_before_river, const std::string &label) {
  const auto expected = hu6_hand_derived_terminals(bets_before_river);
  std::map<std::string, std::uint32_t> paths;
  collect_terminal_paths(plain, plain.root(), "", paths);
  std::map<std::string, std::uint32_t> raked_paths;
  collect_terminal_paths(raked, raked.root(), "", raked_paths);
  require(paths == raked_paths, label + ": both games reach the same terminals by the same lines");
  require(paths.size() == expected.size() &&
              paths.size() == plain.stats().terminal_folds + plain.stats().terminal_showdowns,
          label + ": the tree has exactly the hand-derived terminals, " +
              std::to_string(paths.size()) + " found");
  for (const auto &terminal : expected) {
    const auto found = paths.find(terminal.path);
    require(found != paths.end(), label + ": terminal " + terminal.path + " exists");
    const auto id = found->second;
    const auto &node = plain.nodes()[id];
    const bool showdown = terminal.fold_winner == 0U;
    const auto what = label + " " + terminal.path;
    require(node.kind == (showdown ? pb::NodeKind::TerminalShowdown : pb::NodeKind::TerminalFold) &&
                node.street == terminal.street &&
                (showdown ? node.remaining_board_cards == terminal.remaining_board_cards
                          : plain.states()[id].terminal_winner_mask == terminal.fold_winner),
            what + ": terminal kind, street, and cards to come or fold winner");
    require(plain.states()[id].pot.units() == terminal.pot &&
                raked.states()[id].pot.units() == terminal.pot,
            what + ": called pot " + std::to_string(terminal.pot) + ", got " +
                std::to_string(plain.states()[id].pot.units()));
    const auto rows = showdown ? std::vector<std::uint8_t>{1U, 2U, 3U}
                               : std::vector<std::uint8_t>{terminal.fold_winner};
    for (const auto winners : rows) {
      const auto unraked_payoffs =
          showdown ? plain.showdown_payoffs(id, winners) : plain.fold_payoffs(id);
      const auto raked_payoffs =
          showdown ? raked.showdown_payoffs(id, winners) : raked.fold_payoffs(id);
      for (std::uint8_t player = 0U; player < 2U; ++player) {
        const auto seat =
            what + " winners " + std::to_string(winners) + " seat " + std::to_string(player);
        require(unraked_payoffs[player] == hand_net(terminal.unraked, winners, player),
                seat + ": unraked payoff equals the hand-derived value, got " +
                    std::to_string(unraked_payoffs[player]));
        require(raked_payoffs[player] == hand_net(terminal.raked, winners, player),
                seat + ": raked payoff equals the hand-derived value, got " +
                    std::to_string(raked_payoffs[player]));
        const std::int64_t share = winners == 0b11U                   ? terminal.rake / 2
                                   : ((winners >> player) & 1U) != 0U ? terminal.rake
                                                                      : 0;
        require(raked_payoffs[player] - unraked_payoffs[player] == -share,
                seat + ": the rake comes out of the winners' share only");
      }
    }
  }
}

// Census of check_raked_payoffs on the HU6 pairs: 3 preflop folds (never
// raked), 2 called preflop all-ins, 2 folds and 2 called all-ins per betting
// street, the river checkdown; every terminal after the flop is raked below
// the cap, nothing is odd.
RakeCensus hu6_expected_census(const bool bets_before_river, const bool raked) {
  const std::uint64_t betting_streets = bets_before_river ? 3U : 1U;
  RakeCensus census;
  census.preflop_folds = 3U;
  census.preflop_showdowns = 2U;
  census.postflop_folds = 2U * betting_streets;
  // The two called river all-ins and the river checkdown.
  census.postflop_showdowns = 3U;
  // Called all-ins on the flop and the turn.
  census.postflop_all_ins = 2U * (betting_streets - 1U);
  const auto flop_dealt = census.preflop_showdowns + census.postflop_folds +
                          census.postflop_showdowns + census.postflop_all_ins;
  census.flop_unraked = raked ? 0U : flop_dealt;
  census.below_cap = raked ? flop_dealt : 0U;
  census.cap_binding = 0U;
  census.preflop_fold_rake = 0;
  census.postflop_fold_minimum_rake = raked ? 1'000 : 0;
  census.odd_ties = 0U;
  return census;
}

void require_census(const RakeCensus &actual, const RakeCensus &expected,
                    const std::string &label) {
  require(actual.preflop_folds == expected.preflop_folds &&
              actual.preflop_showdowns == expected.preflop_showdowns &&
              actual.postflop_folds == expected.postflop_folds &&
              actual.postflop_showdowns == expected.postflop_showdowns &&
              actual.postflop_all_ins == expected.postflop_all_ins,
          label + ": terminal census (preflop folds, preflop all-ins, postflop folds, river "
                  "showdowns, postflop all-ins)");
  require(actual.flop_unraked == expected.flop_unraked && actual.below_cap == expected.below_cap &&
              actual.cap_binding == expected.cap_binding &&
              actual.preflop_fold_rake == expected.preflop_fold_rake &&
              actual.postflop_fold_minimum_rake == expected.postflop_fold_minimum_rake &&
              actual.odd_ties == expected.odd_ties,
          label + ": rake census, flop unraked " + std::to_string(actual.flop_unraked) +
              ", below the cap " + std::to_string(actual.below_cap) + ", cap binding " +
              std::to_string(actual.cap_binding) + ", odd ties " + std::to_string(actual.odd_ties));
}

// U1 of the rake study: the payoffs of the HU6 correctness games against a
// table derived by hand, independently of settle_terminal (which
// test_payoffs compares with), for HU6_all and its raked twin (the literal
// P3 config) and for HU6_V2_river and its raked twin V2R (built here; the
// config file, when it exists, must be the same game). The raked and the
// unraked tree are identical node by node.
void test_hu6_rake_payoffs() {
  const auto directory = std::filesystem::path("benchmarks") / "monker" / "correctness";
  const auto all_config = load_config(directory / "HU6_all.json");
  const auto all_raked_config = load_config(directory / "HU6_all_rake25cap2.json");
  auto expected_raked = all_config;
  expected_raked.id = all_raked_config.id;
  expected_raked.rake = correctness_rake();
  require(all_raked_config == expected_raked,
          "HU6_all_rake25cap2.json is HU6_all.json with 2.5 %, cap 2 antes, no flop no drop");

  const auto v2_config = load_config(directory / "HU6_V2_river.json");
  auto v2_raked_config = v2_config;
  v2_raked_config.id = "CORRECTNESS-HU6-V2-RIVER-RAKE25-CAP2-001";
  v2_raked_config.rake = correctness_rake();
  require(pb::validate_game_config(v2_raked_config).has_value(), "the raked V2 twin validates");
  const auto v2_raked_file = directory / "HU6_V2_river_rake25cap2.json";
  if (std::filesystem::exists(std::filesystem::path(GTOSD_SOURCE_DIR) / v2_raked_file)) {
    auto loaded = load_config(v2_raked_file);
    loaded.id = v2_raked_config.id;
    require(loaded == v2_raked_config,
            "HU6_V2_river_rake25cap2.json is HU6_V2_river.json with 2.5 %, cap 2 antes, no flop "
            "no drop");
  }

  struct Twin {
    std::string_view label;
    const pb::GameConfig *plain;
    const pb::GameConfig *raked;
    bool bets_before_river;
    std::uint64_t nodes;
  };
  for (const auto &twin : {Twin{"HU6_all", &all_config, &all_raked_config, true, 37U},
                           Twin{"HU6_V2_river", &v2_config, &v2_raked_config, false, 25U}}) {
    const std::string label(twin.label);
    const auto plain = compile(*twin.plain);
    const auto raked = compile(*twin.raked);
    require(plain.stats().node_count == twin.nodes, label + ": node count");
    require_same_tree(plain, raked, label + " raked");
    require(plain.fingerprint() != raked.fingerprint(),
            label + ": the rake enters the tree fingerprint through the configuration");
    check_hand_derived_terminals(plain, raked, twin.bets_before_river, label);
    const auto raked_census = check_raked_payoffs(raked);
    require_census(raked_census, hu6_expected_census(twin.bets_before_river, true),
                   label + " raked");
    require_census(check_raked_payoffs(plain), hu6_expected_census(twin.bets_before_river, false),
                   label + " unraked");
    std::cout << label << " hand-derived payoffs: "
              << hu6_hand_derived_terminals(twin.bets_before_river).size()
              << " terminals, raked tree " << raked.fingerprint() << ", preflop folds "
              << raked_census.preflop_folds << " unraked, below the cap " << raked_census.below_cap
              << ", cap binding " << raked_census.cap_binding << ", odd ties "
              << raked_census.odd_ties << '\n';
  }
}

// U2 of the rake study: the correctness rake with a minimum pot above every
// pot (13a; the largest pot is 12a, both stacks) is enabled but never taken,
// so every payoff equals the unraked one and only the fingerprints move. The
// same rake with no minimum pot does change the payoffs, and a minimum pot of
// exactly 12a leaves the 4a pots unraked and rakes the 12a ones (the minimum
// is inclusive). HU6_V2_river_rakeinert.json, when it exists, must be the
// V2 game with such an inert rake.
void test_hu6_rake_inert() {
  const auto directory = std::filesystem::path("benchmarks") / "monker" / "correctness";
  struct Inert {
    std::string_view plain;
    std::string_view id;
    std::string_view file;
    bool bets_before_river;
    // Terminals after the flop with a 4a and a 12a pot.
    std::uint64_t small_pots;
    std::uint64_t large_pots;
  };
  for (const auto &inert :
       {Inert{"HU6_all.json", "CORRECTNESS-HU6-ALL-RAKEINERT-001", "", true, 7U, 8U},
        Inert{"HU6_V2_river.json", "CORRECTNESS-HU6-V2-RIVER-RAKEINERT-001",
              "HU6_V2_river_rakeinert.json", false, 3U, 4U}}) {
    const std::string label(inert.plain);
    const auto plain_config = load_config(directory / std::string(inert.plain));
    const auto plain = compile(plain_config);
    std::int64_t largest_pot = 0;
    for (const auto &node : plain.nodes()) {
      if (node.kind == pb::NodeKind::TerminalFold || node.kind == pb::NodeKind::TerminalShowdown) {
        largest_pot = std::max<std::int64_t>(largest_pot, plain.states()[node.id].pot.units());
      }
    }
    require(largest_pot == 120'000, label + ": the largest pot is 12a, both stacks");

    auto inert_config = plain_config;
    inert_config.id = std::string(inert.id);
    inert_config.rake = correctness_rake();
    inert_config.rake.minimum_pot = antes(13);
    require(pb::validate_game_config(inert_config).has_value(),
            label + ": a minimum pot above every pot is a valid rake");
    std::vector<pb::GameConfig> inert_configs{inert_config};
    if (!inert.file.empty()) {
      const auto relative = directory / std::string(inert.file);
      if (std::filesystem::exists(std::filesystem::path(GTOSD_SOURCE_DIR) / relative)) {
        const auto loaded = load_config(relative);
        require(loaded.rake.enabled && loaded.rake.minimum_pot.units() > largest_pot,
                std::string(inert.file) + ": the rake is enabled with a minimum pot above 12a");
        auto normalized = loaded;
        normalized.id = plain_config.id;
        normalized.rake = plain_config.rake;
        require(normalized == plain_config,
                std::string(inert.file) + " is " + label + " plus an inert rake");
        inert_configs.push_back(loaded);
      }
    }
    for (const auto &config : inert_configs) {
      const auto game = compile(config);
      require_same_tree(plain, game, config.id);
      require(same_payoffs(plain, game),
              config.id + ": every payoff equals the unraked one element by element");
      require(pb::game_config_fingerprint(config) != pb::game_config_fingerprint(plain_config),
              config.id + ": only the configuration, hence the fingerprint, differs");
      require_census(check_raked_payoffs(game), hu6_expected_census(inert.bets_before_river, false),
                     config.id);
      std::cout << config.id << " (minimum pot " << config.rake.minimum_pot.units()
                << " units): payoffs identical to " << label << ", tree " << game.fingerprint()
                << " vs " << plain.fingerprint() << '\n';
    }

    // Power of the check: the same rake without the minimum pot is taken.
    auto active = inert_config;
    active.rake.minimum_pot = units(0);
    const auto active_game = compile(active);
    require(!same_payoffs(plain, active_game),
            label + ": without the minimum pot the same rake changes the payoffs");
    // The boundary: a pot equal to the minimum is raked.
    auto boundary = inert_config;
    boundary.rake.minimum_pot = antes(12);
    const auto boundary_census = check_raked_payoffs(compile(boundary));
    require(boundary_census.flop_unraked == inert.small_pots &&
                boundary_census.below_cap == inert.large_pots &&
                boundary_census.cap_binding == 0U && boundary_census.preflop_fold_rake == 0,
            label + ": a 12a minimum pot leaves the 4a pots unraked and rakes the 12a pots, got " +
                std::to_string(boundary_census.flop_unraked) + " unraked and " +
                std::to_string(boundary_census.below_cap) + " raked");
  }
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
    test_open_sizes_config_key();
    test_hu50_pot_mode_equivalence();
    test_three_way_monker_preflop();
    test_three_way_full_counts();
    test_donk_bet_config_key();
    test_tree_fingerprints_without_the_flag(co40);
    test_no_donk_bets();
    test_all_in_cap_config_key();
    test_fingerprints_without_the_cap();
    test_postflop_all_in_cap();
    test_rake_config_key();
    test_raked_payoffs();
    test_checkdown_rake();
    test_rake_fingerprints();
    test_three_way_rake();
    test_hu6_rake_payoffs();
    test_hu6_rake_inert();
    test_betting_streets_config_key();
    test_betting_streets_trees();
    std::cout << "CO40 tree fingerprint " << co40.fingerprint() << '\n';
    std::cout << "PREFLOP_BLUEPRINT_GAME_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_GAME_TESTS=FAIL " << error.what() << " after " << assertions
              << " assertions\n";
    return 1;
  }
}
