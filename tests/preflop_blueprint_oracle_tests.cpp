// Oracle test of the P5 kernels against the standalone postflop solver.
//
// Three river subgames with fixed ranges are solved for a few iterations by
// the postflop solver (used here only as a test oracle, decision D5). Its
// average strategy is copied per combo into a HandPolicy of the compiled
// subgame built by the blueprint library from the same public root state, and
// the root counterfactual values of the vector traversal are compared with
// the exact per-combo values reported by the postflop solver.

#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/postflop/root_values.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"
#include "gtosd/tree/config.hpp"
#include "gtosd/tree/tree.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;

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

gtosd::CardId card(const std::string_view text) {
  const auto parsed = gtosd::parse_card(text);
  require(parsed.has_value(), "card parses");
  return parsed.value();
}

gtosd::Money units(const std::int64_t value) { return gtosd::Money::from_units(value).value(); }

struct OracleCase {
  std::string name;
  std::array<std::string_view, 5> board;
  std::int64_t pot_units;
  std::int64_t stack_units;
  std::vector<std::uint32_t> sizes_basis_points;
  std::uint8_t raise_depth;
  std::uint64_t iterations;
};

gtosd::PostflopTreeConfig make_oracle_config(const OracleCase &test_case) {
  gtosd::PostflopTreeConfig config;
  std::array<gtosd::CardId, 3> flop{card(test_case.board[0]), card(test_case.board[1]),
                                    card(test_case.board[2])};
  std::sort(flop.begin(), flop.end());
  config.flop = flop;
  config.turn = card(test_case.board[3]);
  config.river = card(test_case.board[4]);
  config.initial_pot = units(test_case.pot_units);
  config.effective_stack = units(test_case.stack_units);
  std::vector<gtosd::PotPercentage> sizes;
  for (const auto basis_points : test_case.sizes_basis_points) {
    sizes.push_back(gtosd::PotPercentage::from_basis_points(basis_points).value());
  }
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.aggressive_sizes = sizes;
        scenario.raise_depth = test_case.raise_depth;
        scenario.all_in_mode = gtosd::AllInMode::Add;
        scenario.all_in_threshold = gtosd::PotPercentage::from_basis_points(100'000).value();
        scenario.all_in_strict_boundary = true;
        scenario.minimum_bet = gtosd::Money::from_antes(1).value();
      }
    }
  }
  require(gtosd::validate_tree_config(config).has_value(), "oracle tree configuration validates");
  return config;
}

pb::GameConfig make_blueprint_config(const OracleCase &test_case) {
  const auto path = std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "fixtures" /
                    "preflop_blueprint_co40_v1.json";
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), "CO40 fixture parses");
  auto config = parsed.value();
  config.id = "PREFLOP-BLUEPRINT-ORACLE-" + test_case.name;
  config.postflop_sizes.clear();
  for (const auto basis_points : test_case.sizes_basis_points) {
    config.postflop_sizes.push_back(gtosd::PotPercentage::from_basis_points(basis_points).value());
  }
  require(pb::validate_game_config(config).has_value(), "blueprint configuration validates");
  return config;
}

// Deterministic asymmetric ranges: a weight in {0, 1/4, 1/2, 3/4, 1} per combo.
gtosd::PostflopRanges make_ranges(const std::uint64_t board_mask, const std::uint64_t salt) {
  gtosd::PostflopRanges ranges;
  const auto combos = gtosd::all_combos();
  for (std::uint8_t player = 0; player < 2U; ++player) {
    for (std::size_t combo = 0; combo < combos.size(); ++combo) {
      const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
      std::uint32_t basis_points = 0U;
      if ((mask & board_mask) == 0U) {
        const auto pattern = (combo * 2654435761ULL + salt * 40503ULL + player * 97ULL) % 5ULL;
        basis_points = static_cast<std::uint32_t>(pattern) * 2'500U;
      }
      ranges.players[player][combo] = gtosd::RangeWeight::from_basis_points(basis_points).value();
    }
  }
  return ranges;
}

struct Pairing {
  std::vector<gtosd::NodeId> oracle_node_of; // indexed by compiled node id
  std::uint64_t paired_nodes{0U};
};

void pair_trees(const pb::CompiledGame &game, const std::uint32_t node,
                const gtosd::PublicTree &tree, const gtosd::NodeId oracle_node, Pairing &pairing) {
  const auto &mine = game.nodes()[node];
  const auto &theirs = tree.nodes[oracle_node];
  require(game.states()[node] == theirs.state, "paired nodes share the public state");
  switch (mine.kind) {
  case pb::NodeKind::Decision:
    require(theirs.kind == gtosd::PublicNodeKind::Decision, "decision pairs with decision");
    break;
  case pb::NodeKind::TerminalFold:
    require(theirs.kind == gtosd::PublicNodeKind::TerminalFold, "fold pairs with fold");
    break;
  case pb::NodeKind::TerminalShowdown:
    require(theirs.kind == gtosd::PublicNodeKind::TerminalShowdown, "showdown pairs with showdown");
    break;
  case pb::NodeKind::Chance:
    throw std::runtime_error("a river subgame has no chance node");
  }
  pairing.oracle_node_of[node] = oracle_node;
  ++pairing.paired_nodes;
  const auto edges = game.edges_of(node);
  require(edges.size() == theirs.edges.size(), "paired nodes have the same number of actions");
  for (std::size_t index = 0; index < edges.size(); ++index) {
    require(theirs.edges[index].kind == gtosd::PublicEdgeKind::Action &&
                edges[index].action == theirs.edges[index].action,
            "paired actions are identical and in the same order");
    pair_trees(game, edges[index].child, tree, theirs.edges[index].child, pairing);
  }
}

void run_case(const OracleCase &test_case, const ca::RankTable &ranks) {
  const auto started = Clock::now();
  const auto oracle_config = make_oracle_config(test_case);
  const auto blueprint_config = make_blueprint_config(test_case);
  std::uint64_t board_mask = 0U;
  for (const auto &text : test_case.board) {
    board_mask |= card(text).mask();
  }
  const auto ranges = make_ranges(board_mask, test_case.pot_units);
  require(gtosd::validate_postflop_ranges(oracle_config, ranges).has_value(), "ranges validate");

  // Oracle: a few iterations of the production schedule, then the exact root values.
  gtosd::PostflopProductionSolveRequest request;
  request.iterations = test_case.iterations;
  const auto options = gtosd::resolve_postflop_production_options(request, nullptr);
  require(options.has_value(), "production options resolve");
  const auto solved = gtosd::solve_postflop_exact(oracle_config, ranges, options.value());
  require(solved.has_value(),
          std::string("oracle solve succeeds: ") +
              (solved ? "" : gtosd::postflop_solver_error_name(solved.error())));
  const auto &checkpoint = solved.value().checkpoint;
  const auto oracle_values =
      gtosd::derive_postflop_root_counterfactual_values(oracle_config, ranges, checkpoint);
  require(oracle_values.has_value(), "oracle exposes root counterfactual values");
  const auto tree = gtosd::build_public_tree(oracle_config);
  require(tree.has_value(), "oracle public tree builds");

  // Blueprint: the same river subgame compiled from the same root state.
  const auto root = gtosd::make_hu_postflop_state(gtosd::Street::River, oracle_config.initial_pot,
                                                  oracle_config.effective_stack, board_mask);
  require(root.has_value(), "river root state builds");
  const auto game = pb::CompiledGame::compile_subgame(blueprint_config, root.value());
  require(game.has_value(), "blueprint subgame compiles");
  require(game.value().stats().maximum_raise_count <= test_case.raise_depth,
          "natural raise termination stays within the oracle raise depth");
  Pairing pairing;
  pairing.oracle_node_of.assign(game.value().nodes().size(), 0U);
  pair_trees(game.value(), game.value().root(), tree.value(), tree.value().root, pairing);
  require(pairing.paired_nodes == game.value().nodes().size() &&
              pairing.paired_nodes == tree.value().nodes.size(),
          "the two trees have the same nodes");

  ca::BoardHistory history;
  history.flop = oracle_config.flop;
  history.turn = oracle_config.turn.value();
  history.river = oracle_config.river.value();
  const auto context = pb::BoardContext::build(history, ranks);
  require(context.has_value(), "board context builds");

  // Copy the oracle's average strategy per combo into the per-hand policy.
  pb::HandPolicy policy(game.value());
  std::uint64_t copied_rows = 0U;
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    const auto queries = gtosd::query_postflop_strategies(oracle_config, ranges, checkpoint,
                                                          pairing.oracle_node_of[node.id]);
    require(queries.has_value(), "oracle strategies are queryable");
    const auto edges = game.value().edges_of(node.id);
    for (const auto &query : queries.value()) {
      const auto hand = context.value().hand_index(query.combo);
      require(hand != pb::no_hand, "queried combos are live on the board");
      require(query.actions.size() == edges.size() && query.probabilities.size() == edges.size(),
              "oracle query lists every action");
      auto row = policy.row(node.id, hand);
      double total = 0.0;
      for (std::size_t index = 0; index < query.actions.size(); ++index) {
        std::size_t target = edges.size();
        for (std::size_t edge = 0; edge < edges.size(); ++edge) {
          if (edges[edge].action == query.actions[index]) {
            target = edge;
          }
        }
        require(target < edges.size(), "oracle action maps onto a compiled edge");
        row[target] = query.probabilities[index];
        total += query.probabilities[index];
      }
      require(std::abs(total - 1.0) <= 1e-9, "oracle strategy rows are distributions");
      ++copied_rows;
    }
  }
  require(copied_rows > 0U, "at least one strategy row copied");

  const pb::HeadsUpShowdownKernel kernel;
  pb::ValueTraversal traversal(game.value(), context.value(), kernel, nullptr);
  std::array<double, pb::live_hand_count> reach{};
  std::array<double, pb::live_hand_count> values{};
  std::array<double, pb::live_hand_count> disjoint{};
  double maximum_error = 0.0;
  std::uint64_t compared = 0U;
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    const auto opponent = static_cast<std::uint8_t>(1U - hero);
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      reach[hand] = ranges.players[opponent][context.value().combo_ids()[hand]].basis_points() /
                    10'000.0;
    }
    require(traversal.evaluate(policy, hero, reach, values).has_value(), "traversal evaluates");
    pb::fold_mass(context.value(), reach, disjoint);
    for (const auto &oracle : oracle_values.value().players[hero]) {
      if (!oracle.positive_reach) {
        continue;
      }
      const auto hand = context.value().hand_index(oracle.combo);
      require(hand != pb::no_hand && disjoint[hand] > 0.0, "oracle combo is live with reach");
      const auto conditional = values[hand] / disjoint[hand];
      maximum_error = std::max(maximum_error, std::abs(conditional - oracle.conditional_value_antes));
      require(std::abs(conditional - oracle.conditional_value_antes) <= 1e-9,
              "conditional root value matches the postflop solver within 1e-9 antes");
      ++compared;
    }
  }
  require(compared > 0U, "combos compared");
  std::cout << test_case.name << ": nodes " << game.value().nodes().size() << ", decisions "
            << game.value().stats().decision_nodes << ", max raises "
            << static_cast<unsigned>(game.value().stats().maximum_raise_count) << ", iterations "
            << checkpoint.completed_iterations << ", strategy rows " << copied_rows
            << ", combos compared " << compared << ", max |error| " << maximum_error
            << " antes, " << std::chrono::duration<double>(Clock::now() - started).count()
            << " s\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      if (std::string_view{argv[index]} == "--resources-dir") {
        resources_dir = argv[index + 1];
      } else {
        throw std::runtime_error("unknown argument " + std::string{argv[index]});
      }
    }
    std::optional<ca::RankTable> ranks;
    if (!resources_dir.empty()) {
      auto loaded = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
      if (loaded) {
        ranks.emplace(std::move(loaded.value()));
      }
    }
    if (!ranks) {
      auto built = ca::RankTable::build();
      require(built.has_value(), "rank table builds");
      ranks.emplace(std::move(built.value()));
    }
    const std::vector<OracleCase> cases{
        {"river-limped-pot", {"As", "Qd", "7c", "6h", "9s"}, 40'000, 390'000, {5'000}, 4U, 40U},
        {"river-open-6-paired", {"Ah", "Ad", "9c", "9s", "Tc"}, 140'000, 340'000,
         {3'300, 6'600, 12'000}, 4U, 20U},
        {"river-open-10-flush", {"7s", "8s", "Js", "Qs", "6d"}, 220'000, 300'000,
         {3'300, 6'600, 12'000}, 4U, 20U},
    };
    for (const auto &test_case : cases) {
      run_case(test_case, ranks.value());
    }
    std::cout << "PREFLOP_BLUEPRINT_ORACLE_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_ORACLE_TESTS=FAIL " << error.what() << " after " << assertions
              << " assertions\n";
    return 1;
  }
}
