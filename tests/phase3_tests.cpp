#include "gtosd/tree/tree.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef GTOSD_SOURCE_DIR
#define GTOSD_SOURCE_DIR "."
#endif

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::CardId card(const std::string_view text) {
  const auto parsed = gtosd::parse_card(text);
  require(parsed.has_value(), "card parses");
  return parsed.value();
}

gtosd::Money antes(const std::int64_t value) { return gtosd::Money::from_antes(value).value(); }

gtosd::PotPercentage pct(const std::int64_t value) {
  return gtosd::PotPercentage::from_basis_points(value).value();
}

gtosd::PostflopTreeConfig check_only_config() {
  gtosd::PostflopTreeConfig config;
  config.flop = {card("As"), card("Qd"), card("7c")};
  config.initial_pot = antes(10);
  config.effective_stack = antes(20);
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.minimum_bet = antes(1);
      }
    }
  }
  return config;
}

const gtosd::PublicTreeEdge &action_edge(const gtosd::PublicTreeNode &node,
                                         const gtosd::ActionType type) {
  const auto found = std::ranges::find_if(node.edges, [type](const auto &edge) {
    return edge.kind == gtosd::PublicEdgeKind::Action && edge.action.type == type;
  });
  require(found != node.edges.end(), "requested action edge exists");
  return *found;
}

gtosd::NodeId first_flop_chance(const gtosd::PublicTree &tree) {
  const auto &root = tree.nodes[static_cast<std::size_t>(tree.root)];
  const auto &after_co =
      tree.nodes[static_cast<std::size_t>(action_edge(root, gtosd::ActionType::Check).child)];
  return action_edge(after_co, gtosd::ActionType::Check).child;
}

void test_config_schema_contract() {
  const std::string fixture_path =
      std::string(GTOSD_SOURCE_DIR) + "/tests/fixtures/postflop_check_only.json";
  std::ifstream input(fixture_path, std::ios::binary);
  require(static_cast<bool>(input), "versioned JSON fixture opens");
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto parsed = gtosd::parse_tree_config_json(json);
  require(parsed.has_value(), "fixture matches parser contract");
  require(parsed.value().version == 1U, "configuration version one");
  require(parsed.value().flop[0] == card("As"), "flop preserved");

  const auto round_trip =
      gtosd::parse_tree_config_json(gtosd::serialize_tree_config_json(parsed.value()));
  require(round_trip.has_value(), "configuration JSON round trip");
  require(round_trip.value().initial_pot == parsed.value().initial_pot,
          "round trip preserves money");

  auto scheduled = parsed.value();
  auto &scheduled_raise =
      scheduled.streets[0].players[0][static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)];
  scheduled_raise.aggressive_sizes = {pct(3'300), pct(7'500)};
  scheduled_raise.raise_depth = 2U;
  scheduled_raise.aggressive_sizes_by_raise_count = {{pct(3'300), pct(7'500)}, {pct(7'500)}};
  const auto scheduled_round_trip =
      gtosd::parse_tree_config_json(gtosd::serialize_tree_config_json(scheduled));
  require(scheduled_round_trip.has_value() &&
              scheduled_round_trip.value()
                      .streets[0]
                      .players[0][static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)]
                      .aggressive_sizes_by_raise_count ==
                  scheduled_raise.aggressive_sizes_by_raise_count,
          "per-raise-count sizing schedule round trips");

  scheduled_raise.aggressive_target_rounding = {
      {gtosd::Money::from_units(100'000).value(), gtosd::Money::from_units(1'000).value()},
      {gtosd::Money{}, gtosd::Money::from_units(10'000).value()}};
  const auto rounded_round_trip =
      gtosd::parse_tree_config_json(gtosd::serialize_tree_config_json(scheduled));
  require(rounded_round_trip.has_value() &&
              rounded_round_trip.value()
                      .streets[0]
                      .players[0][static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)]
                      .aggressive_target_rounding == scheduled_raise.aggressive_target_rounding,
          "generic aggressive-target rounding policy round trips");

  scheduled_raise.aggressive_sizes_by_raise_count = {{pct(7'500)}};
  const auto invalid_schedule = gtosd::validate_tree_config(scheduled);
  require(!invalid_schedule && invalid_schedule.error() == gtosd::TreeConfigError::TooManySizes,
          "sizing schedule must cover every configured raise depth");

  auto duplicate_flop = parsed.value();
  duplicate_flop.flop[2] = duplicate_flop.flop[0];
  const auto invalid = gtosd::validate_tree_config(duplicate_flop);
  require(!invalid && invalid.error() == gtosd::TreeConfigError::DuplicateCard,
          "duplicate flop rejected");

  auto too_many = parsed.value();
  too_many.streets[0].players[0][0].aggressive_sizes = {pct(2'500), pct(5'000), pct(7'500),
                                                        pct(10'000)};
  const auto invalid_sizes = gtosd::validate_tree_config(too_many);
  require(!invalid_sizes && invalid_sizes.error() == gtosd::TreeConfigError::TooManySizes,
          "four sizes rejected");
}

void test_fixed_turn_and_river_roots() {
  auto turn_config = check_only_config();
  turn_config.turn = card("Jh");
  const auto serialized = gtosd::serialize_tree_config_json(turn_config);
  const auto restored = gtosd::parse_tree_config_json(serialized);
  require(restored.has_value() && restored.value().turn == turn_config.turn,
          "fixed turn round trips");
  const auto turn_tree = gtosd::build_public_tree(turn_config);
  require(turn_tree.has_value(), "fixed-turn tree builds");
  require(turn_tree.value().nodes[turn_tree.value().root].state.street == gtosd::Street::Turn,
          "four-card board starts on turn");
  require(std::popcount(turn_tree.value().nodes[turn_tree.value().root].state.board_mask) == 4,
          "turn root contains four public cards");

  auto river_config = turn_config;
  river_config.river = card("9s");
  const auto river_tree = gtosd::build_public_tree(river_config);
  require(river_tree.has_value(), "fixed-river tree builds");
  require(river_tree.value().nodes[river_tree.value().root].state.street == gtosd::Street::River,
          "five-card board starts on river");
  require(river_tree.value().stats.chance_nodes == 0U, "fixed river has no public chance nodes");

  auto invalid = check_only_config();
  invalid.river = card("9s");
  require(!gtosd::validate_tree_config(invalid), "river without turn is rejected");
}

void test_streamed_tree_matches_materialized_tree() {
  const auto materialized = gtosd::build_public_tree(check_only_config());
  require(materialized.has_value(), "materialized stream oracle builds");

  struct StreamedNode {
    gtosd::PublicNodeKind kind{gtosd::PublicNodeKind::Decision};
    gtosd::PublicState state{};
    std::uint32_t depth{0};
    std::vector<gtosd::PublicTreeEdge> edges;
  };
  std::vector<StreamedNode> streamed_nodes;
  std::vector<std::vector<std::uint8_t>> streamed_stabilizers;
  const gtosd::PublicTreeStreamConsumer consumer{
      [&](const gtosd::NodeId id, const gtosd::PublicNodeKind kind, const gtosd::PublicState &state,
          const std::uint32_t depth) {
        if (id != streamed_nodes.size()) {
          return false;
        }
        streamed_nodes.push_back({kind, state, depth, {}});
        return true;
      },
      [&](const gtosd::NodeId id, const std::vector<gtosd::PublicTreeEdge> &edges) {
        if (id >= streamed_nodes.size()) {
          return false;
        }
        streamed_nodes[static_cast<std::size_t>(id)].edges = edges;
        return true;
      },
      [&](const gtosd::NodeId id, const std::span<const std::uint8_t> stabilizer) {
        if (id != streamed_stabilizers.size()) {
          return false;
        }
        streamed_stabilizers.emplace_back(stabilizer.begin(), stabilizer.end());
        return true;
      }};
  const auto streamed = gtosd::stream_public_tree(check_only_config(), consumer);
  require(streamed.has_value(), "streamed public tree builds");
  require(streamed.value().root == materialized.value().root,
          "streamed root matches materialized root");
  require(streamed.value().stats.node_count == materialized.value().stats.node_count &&
              streamed.value().stats.edge_count == materialized.value().stats.edge_count &&
              streamed.value().stats.decision_nodes_by_street_player_action ==
                  materialized.value().stats.decision_nodes_by_street_player_action,
          "streamed statistics match materialized tree");
  require(streamed_nodes.size() == materialized.value().nodes.size(),
          "stream emits every materialized node exactly once");
  require(streamed_stabilizers.size() == streamed_nodes.size() &&
              std::ranges::all_of(streamed_stabilizers,
                                  [](const auto &stabilizer) { return stabilizer.empty(); }),
          "stream emits the exact empty stabilizer for every non-canonical node");
  for (std::size_t index = 0U; index < streamed_nodes.size(); ++index) {
    const auto &streamed_node = streamed_nodes[index];
    const auto &materialized_node = materialized.value().nodes[index];
    require(streamed_node.kind == materialized_node.kind &&
                streamed_node.depth == materialized_node.depth &&
                gtosd::serialize_public_state(streamed_node.state) ==
                    gtosd::serialize_public_state(materialized_node.state),
            "streamed node payload matches materialized payload");
    require(streamed_node.edges.size() == materialized_node.edges.size(),
            "streamed edge count matches materialized edge count");
    for (std::size_t edge_index = 0U; edge_index < streamed_node.edges.size(); ++edge_index) {
      const auto &streamed_edge = streamed_node.edges[edge_index];
      const auto &materialized_edge = materialized_node.edges[edge_index];
      require(streamed_edge.kind == materialized_edge.kind &&
                  streamed_edge.child == materialized_edge.child &&
                  streamed_edge.action == materialized_edge.action &&
                  streamed_edge.chance_card == materialized_edge.chance_card &&
                  streamed_edge.physical_outcome_count ==
                      materialized_edge.physical_outcome_count &&
                  streamed_edge.total_legal_outcome_count ==
                      materialized_edge.total_legal_outcome_count &&
                  streamed_edge.chance_outcome_count == materialized_edge.chance_outcome_count,
              "streamed edge payload matches materialized edge payload");
      for (std::size_t outcome = 0U; outcome < streamed_edge.chance_outcome_count; ++outcome) {
        require(streamed_edge.chance_outcomes[outcome].card ==
                        materialized_edge.chance_outcomes[outcome].card &&
                    streamed_edge.chance_outcomes[outcome].physical_to_representative_permutation ==
                        materialized_edge.chance_outcomes[outcome]
                            .physical_to_representative_permutation,
                "streamed chance mapping matches materialized mapping");
      }
    }
  }
}

gtosd::PublicTree test_check_only_physical_tree() {
  const auto first = gtosd::build_public_tree(check_only_config());
  require(first.has_value(), "check-only physical tree builds");
  const auto estimate = gtosd::estimate_public_tree(check_only_config());
  require(estimate.has_value(), "preflight estimate completes without materializing nodes");
  const auto second = gtosd::build_public_tree(check_only_config());
  require(second.has_value(), "same tree rebuilds");
  const auto &tree = first.value();

  require(tree.stats.node_count == 3'270U, "approved check-only snapshot node count");
  require(estimate.value().node_count == tree.stats.node_count,
          "preflight node estimate equals materialized tree");
  require(estimate.value().node_count_by_street == tree.stats.node_count_by_street &&
              estimate.value().edge_count_by_street == tree.stats.edge_count_by_street &&
              estimate.value().decision_nodes_by_street == tree.stats.decision_nodes_by_street &&
              estimate.value().chance_edges_by_street == tree.stats.chance_edges_by_street &&
              estimate.value().action_edges_by_street == tree.stats.action_edges_by_street,
          "preflight per-street counts equal the materialized tree");
  require(estimate.value().estimated_eager_bytes == tree.stats.estimated_eager_bytes,
          "preflight eager memory estimate equals materialized layout estimate");
  require(tree.stats.edge_count == 3'269U, "tree edge count is nodes minus root");
  require(tree.stats.decision_nodes == 2'180U, "decision node snapshot");
  require(tree.stats.chance_nodes == 34U, "chance node snapshot");
  require(tree.stats.terminal_fold_nodes == 0U, "check-only has no folds");
  require(tree.stats.terminal_showdown_nodes == 1'056U,
          "all ordered turn-river runouts reach showdown");
  require(tree.stats.chance_edges == 1'089U, "33 turn plus 33 times 32 river edges");
  require(tree.betting_tree_hash == "fnv1a64:0d2cb83058ae7460",
          "approved check-only snapshot hash");
  require(tree.betting_tree_hash == second.value().betting_tree_hash,
          "betting tree hash deterministic");
  require(tree.betting_tree_hash.starts_with("fnv1a64:"), "versioned hash algorithm");

  for (const auto &node : tree.nodes) {
    require(gtosd::validate_state(node.state).has_value(), "every tree state valid");
    require(std::popcount(node.state.board_mask) >= 3 && std::popcount(node.state.board_mask) <= 5,
            "board mask has unique physical cards");
    if (node.kind == gtosd::PublicNodeKind::Chance) {
      std::uint32_t physical_total = 0;
      for (const auto &edge : node.edges) {
        require(edge.kind == gtosd::PublicEdgeKind::ChanceCard, "chance edge typed");
        require((edge.chance_card.mask() & node.state.board_mask) == 0U,
                "chance card is physically legal");
        require(edge.physical_outcome_count == 1U, "F3 physical child has unit multiplicity");
        require(edge.total_legal_outcome_count == node.edges.size(),
                "chance denominator matches legal outcomes");
        physical_total += edge.physical_outcome_count;
      }
      require(physical_total == node.edges.size(), "chance probabilities sum to one");
    }
    if (node.kind == gtosd::PublicNodeKind::TerminalShowdown) {
      require(std::popcount(node.state.board_mask) == 5, "showdown always has five board cards");
    }
  }

  const auto chance_id = first_flop_chance(tree);
  const auto &chance = tree.nodes[static_cast<std::size_t>(chance_id)];
  require(chance.edges.size() == 33U, "every physical turn present");
  const std::vector<gtosd::CardId> private_cards{card("6c"), card("6d"), card("6h"), card("6s")};
  const auto conditioned = gtosd::condition_chance_edges(tree, chance_id, private_cards);
  require(conditioned.has_value() && conditioned.value().size() == 29U,
          "HU private cards condition 33 public turns to 29 legal turns");
  for (const auto &edge : conditioned.value()) {
    require(edge.total_legal_outcome_count == 29U, "conditioned chance renormalized exactly");
  }
  return first.value();
}

void test_all_in_runout_and_terminal_resolution() {
  auto config = check_only_config();
  config.effective_stack = antes(1);
  auto &lead = config.streets[0].players[0][static_cast<std::size_t>(gtosd::BettingScenario::Lead)];
  lead.aggressive_sizes = {pct(10'000)};

  const auto built = gtosd::build_public_tree(config);
  require(built.has_value(), "shallow all-in tree builds");
  const auto &tree = built.value();
  const auto &root = tree.nodes[static_cast<std::size_t>(tree.root)];
  const auto &facing =
      tree.nodes[static_cast<std::size_t>(action_edge(root, gtosd::ActionType::AllIn).child)];
  const auto &runout =
      tree.nodes[static_cast<std::size_t>(action_edge(facing, gtosd::ActionType::Call).child)];
  require(runout.kind == gtosd::PublicNodeKind::Chance && runout.edges.size() == 33U,
          "flop all-in call deals every turn");
  for (const auto &turn : runout.edges) {
    const auto &river = tree.nodes[static_cast<std::size_t>(turn.child)];
    require(river.kind == gtosd::PublicNodeKind::Chance && river.edges.size() == 32U,
            "all-in turn always deals every river");
    for (const auto &river_edge : river.edges) {
      require(tree.nodes[static_cast<std::size_t>(river_edge.child)].kind ==
                  gtosd::PublicNodeKind::TerminalShowdown,
              "all-in runout terminates only after river");
    }
  }

  const auto terminal_id = runout.edges.front().child;
  const auto &river_chance = tree.nodes[static_cast<std::size_t>(terminal_id)];
  const auto showdown_id = river_chance.edges.front().child;
  const auto &showdown_node = tree.nodes[static_cast<std::size_t>(showdown_id)];
  std::vector<gtosd::CardId> available;
  for (const auto deck_card : gtosd::short_deck()) {
    if ((deck_card.mask() & showdown_node.state.board_mask) == 0U) {
      available.push_back(deck_card);
    }
  }
  const std::vector<std::array<gtosd::CardId, 2>> holes{{available[0], available[1]},
                                                        {available[2], available[3]}};
  const auto resolution = gtosd::resolve_showdown_terminal(tree, showdown_id, holes);
  require(resolution.has_value(), "F2 evaluator and F1 settlement resolve F3 terminal");
  require(resolution.value().showdown.winner_mask != 0U, "showdown winner mask non-empty");
  require(resolution.value().settlement.payoff_units[0] +
                  resolution.value().settlement.payoff_units[1] ==
              0,
          "zero-rake terminal payoff remains zero-sum");

  auto overlapping = holes;
  overlapping[0][0] =
      gtosd::CardId::from_index(
          static_cast<std::uint8_t>(std::countr_zero(showdown_node.state.board_mask)))
          .value();
  const auto invalid = gtosd::resolve_showdown_terminal(tree, showdown_id, overlapping);
  require(!invalid && invalid.error() == gtosd::TreeError::InvalidPrivateCards,
          "private-board overlap fails explicitly");
}

void test_sizing_and_raise_depth_in_tree() {
  auto three_sizes = check_only_config();
  auto &lead =
      three_sizes.streets[0].players[0][static_cast<std::size_t>(gtosd::BettingScenario::Lead)];
  lead.aggressive_sizes = {pct(2'500), pct(5'000), pct(10'000)};
  auto &facing_bet = three_sizes.streets[0]
                         .players[1][static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)];
  facing_bet.aggressive_sizes = {pct(5'000)};
  facing_bet.raise_depth = 0;
  const auto sized_tree = gtosd::build_public_tree(three_sizes);
  require(sized_tree.has_value(), "three-size tree builds");
  const auto &root = sized_tree.value().nodes.front();
  const auto aggressive_count = std::ranges::count_if(root.edges, [](const auto &edge) {
    return edge.action.type == gtosd::ActionType::Bet ||
           edge.action.type == gtosd::ActionType::Raise;
  });
  require(aggressive_count == 3, "three distinct configured non-all-in sizes");
  const auto bet = std::ranges::find_if(root.edges, [](const auto &edge) {
    return edge.kind == gtosd::PublicEdgeKind::Action && edge.action.type == gtosd::ActionType::Bet;
  });
  require(bet != root.edges.end(), "flop bet branch exists");
  const auto &flop_response = sized_tree.value().nodes[static_cast<std::size_t>(bet->child)];
  require(std::ranges::none_of(flop_response.edges,
                               [](const auto &edge) {
                                 return edge.kind == gtosd::PublicEdgeKind::Action &&
                                        edge.action.type == gtosd::ActionType::Raise;
                               }),
          "raise depth zero creates no non-all-in raise");
  const auto &bet_call_chance = sized_tree.value().nodes[static_cast<std::size_t>(
      action_edge(flop_response, gtosd::ActionType::Call).child)];
  require(bet_call_chance.kind == gtosd::PublicNodeKind::Chance &&
              bet_call_chance.edges.size() == 33U,
          "flop bet-call reaches every physical turn");
  const auto &fold_terminal = sized_tree.value().nodes[static_cast<std::size_t>(
      action_edge(flop_response, gtosd::ActionType::Fold).child)];
  require(fold_terminal.kind == gtosd::PublicNodeKind::TerminalFold && fold_terminal.edges.empty(),
          "fold terminal has no children");

  auto river_bet = check_only_config();
  river_bet.streets[2]
      .players[0][static_cast<std::size_t>(gtosd::BettingScenario::Lead)]
      .aggressive_sizes = {pct(5'000)};
  const auto river_tree = gtosd::build_public_tree(river_bet);
  require(river_tree.has_value(), "river betting tree builds");
  auto node_id = first_flop_chance(river_tree.value());
  node_id = river_tree.value().nodes[static_cast<std::size_t>(node_id)].edges.front().child;
  const auto &turn_co = river_tree.value().nodes[static_cast<std::size_t>(node_id)];
  const auto &turn_btn =
      river_tree.value()
          .nodes[static_cast<std::size_t>(action_edge(turn_co, gtosd::ActionType::Check).child)];
  const auto &river_chance =
      river_tree.value()
          .nodes[static_cast<std::size_t>(action_edge(turn_btn, gtosd::ActionType::Check).child)];
  require(river_chance.kind == gtosd::PublicNodeKind::Chance && river_chance.edges.size() == 32U,
          "turn check-check reaches every physical river");
  const auto &river_co =
      river_tree.value().nodes[static_cast<std::size_t>(river_chance.edges.front().child)];
  const auto &river_btn =
      river_tree.value()
          .nodes[static_cast<std::size_t>(action_edge(river_co, gtosd::ActionType::Bet).child)];
  const auto &river_showdown =
      river_tree.value()
          .nodes[static_cast<std::size_t>(action_edge(river_btn, gtosd::ActionType::Call).child)];
  require(river_showdown.kind == gtosd::PublicNodeKind::TerminalShowdown,
          "river call reaches terminal showdown");

  auto depth_four = check_only_config();
  depth_four.effective_stack = antes(100);
  for (std::size_t player = 0; player < 2U; ++player) {
    auto &flop = depth_four.streets[0].players[player];
    flop[static_cast<std::size_t>(gtosd::BettingScenario::Lead)].aggressive_sizes = {pct(2'500)};
    flop[static_cast<std::size_t>(gtosd::BettingScenario::AfterCheck)].aggressive_sizes = {
        pct(2'500)};
    auto &facing = flop[static_cast<std::size_t>(gtosd::BettingScenario::FacingBet)];
    facing.aggressive_sizes = {pct(2'500)};
    facing.raise_depth = 4;
  }
  const auto raised_tree = gtosd::build_public_tree(depth_four, gtosd::TreeBuildOptions{1'000'000});
  require(raised_tree.has_value(), "raise-depth-four tree builds under explicit node budget");
  bool saw_depth_four = false;
  for (const auto &node : raised_tree.value().nodes) {
    if (node.state.raise_count_this_street == 4U && node.kind == gtosd::PublicNodeKind::Decision) {
      saw_depth_four = true;
      require(std::ranges::none_of(node.edges,
                                   [](const auto &edge) {
                                     return edge.kind == gtosd::PublicEdgeKind::Action &&
                                            edge.action.type == gtosd::ActionType::Raise;
                                   }),
              "no fifth non-all-in raise generated");
    }
  }
  require(saw_depth_four, "tree reaches configured fourth raise");

  const auto limited = gtosd::build_public_tree(check_only_config(), gtosd::TreeBuildOptions{100});
  require(!limited && limited.error() == gtosd::TreeError::BuildLimitExceeded,
          "eager build respects explicit node budget");
}

} // namespace

int main() {
  try {
    test_config_schema_contract();
    test_fixed_turn_and_river_roots();
    test_streamed_tree_matches_materialized_tree();
    const auto tree = test_check_only_physical_tree();
    require(!tree.betting_tree_hash.empty(), "snapshot hash available to inspector");
    test_all_in_runout_and_terminal_resolution();
    test_sizing_and_raise_depth_in_tree();
    std::cout << "F3_PUBLIC_TREE_TESTS=PASS\n"
              << "assertions=" << assertions << '\n'
              << "check_only_nodes=3270\n"
              << "ordered_flop_runouts=1056\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F3_PUBLIC_TREE_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "F3_PUBLIC_TREE_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
