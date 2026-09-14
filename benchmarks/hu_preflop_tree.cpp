#include "gtosd/preflop/hu_preflop.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;

constexpr std::uint32_t all_in_threshold_basis_points = 100'000U;

double in_antes(const gtosd::Money value) {
  return static_cast<double>(value.units()) /
         static_cast<double>(gtosd::Money::units_per_ante);
}

std::string_view player_name(const std::uint8_t player) {
  return player == 0U ? "CO" : "BTN";
}

std::string_view street_name(const gtosd::Street street) {
  switch (street) {
  case gtosd::Street::Preflop:
    return "preflop";
  case gtosd::Street::Flop:
    return "flop";
  case gtosd::Street::Turn:
    return "turn";
  case gtosd::Street::River:
    return "river";
  }
  throw std::runtime_error("unknown street");
}

std::string_view status_name(const gtosd::HandStatus status) {
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
  throw std::runtime_error("unknown hand status");
}

std::string_view action_type_name(const gtosd::ActionType type) {
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
  throw std::runtime_error("unknown action type");
}

std::string format_antes(const gtosd::Money value) {
  std::ostringstream output;
  output << in_antes(value);
  return output.str();
}

std::string action_label(const gtosd::Action &action, const gtosd::PublicState &next,
                         const std::uint8_t actor) {
  switch (action.type) {
  case gtosd::ActionType::Fold:
    return "Fold";
  case gtosd::ActionType::Check:
    return "Check";
  case gtosd::ActionType::Call:
    return "Call " + format_antes(action.amount) + "a";
  case gtosd::ActionType::Bet:
    return "Bet to " + format_antes(next.committed_this_street[actor]) + "a";
  case gtosd::ActionType::Raise:
    return "Raise to " + format_antes(next.committed_this_street[actor]) + "a";
  case gtosd::ActionType::AllIn:
    return "All-in to " + format_antes(next.committed_this_street[actor]) + "a";
  }
  throw std::runtime_error("unknown action type");
}

gtosd::ActionConfig postflop_action_config(const gtosd::HuPreflopConfig &config) {
  gtosd::ActionConfig result;
  result.aggressive_sizes.assign(config.postflop_sizes.begin(), config.postflop_sizes.end());
  result.raise_depth = gtosd::maximum_core_raise_depth;
  result.minimum_bet = config.postflop_minimum_bet;
  if (config.include_all_in) {
    result.all_in_mode = gtosd::AllInMode::Add;
    result.all_in_threshold =
        gtosd::PotPercentage::from_basis_points(all_in_threshold_basis_points).value();
  }
  return result;
}

Json state_json(const gtosd::PublicState &state) {
  const auto actor = state.player_to_act;
  return {
      {"street", street_name(state.street)},
      {"status", status_name(state.status)},
      {"player", player_name(actor)},
      {"potAnte", in_antes(state.pot)},
      {"initialPotAnte", in_antes(state.initial_pot)},
      {"currentBetAnte", in_antes(state.current_bet)},
      {"toCallAnte", in_antes(gtosd::amount_to_call(state, actor))},
      {"remainingStackAnte",
       Json::array({in_antes(state.remaining_stacks[0]), in_antes(state.remaining_stacks[1])})},
      {"committedStreetAnte",
       Json::array(
           {in_antes(state.committed_this_street[0]), in_antes(state.committed_this_street[1])})},
      {"committedTotalAnte",
       Json::array({in_antes(state.committed_total[0]), in_antes(state.committed_total[1])})},
      {"initialContributionAnte",
       Json::array({in_antes(state.initial_pot_contributions[0]),
                    in_antes(state.initial_pot_contributions[1])})},
      {"raiseCount", state.raise_count_this_street},
  };
}

struct ExportCounts {
  std::uint64_t represented_nodes{0U};
  std::uint64_t decision_nodes{0U};
  std::uint64_t action_edges{0U};
  std::uint64_t chance_frontiers{0U};
  std::uint64_t terminal_folds{0U};
  std::uint64_t terminal_showdowns{0U};
  std::uint64_t terminal_all_in_runouts{0U};
  std::uint32_t maximum_depth{0U};
  std::uint8_t maximum_raise_count{0U};
};

class PostflopTreeExporter {
public:
  explicit PostflopTreeExporter(const gtosd::HuPreflopConfig &config)
      : action_config_(postflop_action_config(config)) {}

  Json add_entry(const std::uint32_t entry_node, const gtosd::PublicState &state) {
    Json entry{{"preflopNode", entry_node}};
    entry["target"] = visit(entry_node, state, 0U);
    return entry;
  }

  [[nodiscard]] const Json &nodes() const noexcept { return nodes_; }
  [[nodiscard]] const ExportCounts &counts() const noexcept { return counts_; }

private:
  Json visit(const std::uint32_t entry_node, const gtosd::PublicState &state,
             const std::uint32_t depth) {
    ++counts_.represented_nodes;
    counts_.maximum_depth = std::max(counts_.maximum_depth, depth);
    counts_.maximum_raise_count =
        std::max(counts_.maximum_raise_count, state.raise_count_this_street);

    if (state.status == gtosd::HandStatus::Folded) {
      ++counts_.terminal_folds;
      return {{"kind", "terminal"},
              {"status", status_name(state.status)},
              {"state", state_json(state)}};
    }
    if (state.status == gtosd::HandStatus::Showdown) {
      ++counts_.terminal_showdowns;
      return {{"kind", "terminal"},
              {"status", status_name(state.status)},
              {"state", state_json(state)}};
    }
    if (state.status == gtosd::HandStatus::AllInRunout) {
      ++counts_.terminal_all_in_runouts;
      return {{"kind", "terminal"},
              {"status", status_name(state.status)},
              {"state", state_json(state)}};
    }
    if (state.status == gtosd::HandStatus::StreetComplete) {
      ++counts_.chance_frontiers;
      const auto next = gtosd::advance_street(state);
      if (!next) {
        throw std::runtime_error("cannot advance postflop street");
      }
      return {{"kind", "chance"},
              {"deals", street_name(next.value().street)},
              {"next", visit(entry_node, next.value(), depth + 1U)}};
    }
    if (state.status != gtosd::HandStatus::InProgress) {
      throw std::runtime_error("unsupported postflop state");
    }
    if (nodes_.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("postflop export node overflow");
    }

    ++counts_.decision_nodes;
    const auto node_id = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(Json{});
    const auto actions = gtosd::legal_actions(state, action_config_);
    if (!actions || actions.value().empty()) {
      throw std::runtime_error("cannot enumerate legal postflop actions");
    }

    Json serialized_actions = Json::array();
    for (const auto &action : actions.value()) {
      const auto next = gtosd::apply_action(state, action, action_config_);
      if (!next) {
        throw std::runtime_error("cannot apply legal postflop action");
      }
      ++counts_.action_edges;
      const auto actor = state.player_to_act;
      serialized_actions.push_back(
          {{"type", action_type_name(action.type)},
           {"label", action_label(action, next.value(), actor)},
           {"paymentAnte", in_antes(action.amount)},
           {"targetCommitmentAnte", in_antes(next.value().committed_this_street[actor])},
           {"requestedBasisPoints", action.requested_basis_points},
           {"target", visit(entry_node, next.value(), depth + 1U)}});
    }
    nodes_[node_id] = {{"id", node_id},
                       {"entryNode", entry_node},
                       {"depth", depth},
                       {"state", state_json(state)},
                       {"actions", std::move(serialized_actions)}};
    return {{"kind", "decision"}, {"node", node_id}};
  }

  gtosd::ActionConfig action_config_{};
  Json nodes_{Json::array()};
  ExportCounts counts_{};
};

using EntryHistoryMap = std::map<std::uint32_t, Json>;

void collect_entry_histories(const gtosd::HuPreflopTree &tree, const std::uint32_t node_id,
                             const Json &history, EntryHistoryMap &entries) {
  if (node_id >= tree.nodes.size()) {
    throw std::runtime_error("preflop tree edge points outside the node catalog");
  }
  const auto &node = tree.nodes[node_id];
  if (node.kind == gtosd::HuPreflopNodeKind::PostflopEntry) {
    entries.emplace(node.id, history);
    return;
  }
  if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
    return;
  }
  for (const auto &edge : node.edges) {
    const auto &child = tree.nodes.at(edge.child);
    Json next_history = history;
    next_history.push_back({{"player", player_name(node.state.player_to_act)},
                            {"action",
                             action_label(edge.action, child.state, node.state.player_to_act)}});
    collect_entry_histories(tree, edge.child, next_history, entries);
  }
}

void require_matching_counts(const ExportCounts &actual,
                             const gtosd::HuPostflopPublicStats &expected) {
  if (actual.represented_nodes != expected.represented_nodes ||
      actual.decision_nodes != expected.decision_nodes ||
      actual.action_edges != expected.action_edges ||
      actual.chance_frontiers != expected.chance_frontiers ||
      actual.terminal_folds != expected.terminal_folds ||
      actual.terminal_showdowns != expected.terminal_showdowns ||
      actual.terminal_all_in_runouts != expected.terminal_all_in_runouts ||
      actual.maximum_depth != expected.maximum_subtree_depth ||
      actual.maximum_raise_count != expected.maximum_observed_raise_count) {
    throw std::runtime_error("postflop export counts differ from the certified public skeleton");
  }
}

void export_postflop_tree(const gtosd::HuPreflopTree &tree,
                          const gtosd::HuPostflopPublicStats &expected,
                          const std::string &output_path) {
  EntryHistoryMap histories;
  collect_entry_histories(tree, tree.root, Json::array(), histories);
  if (histories.size() != tree.stats.postflop_entries) {
    throw std::runtime_error("preflop entry history coverage is incomplete");
  }

  PostflopTreeExporter exporter(tree.config);
  Json entries = Json::array();
  for (const auto &[entry_node, history] : histories) {
    auto entry = exporter.add_entry(entry_node, tree.nodes.at(entry_node).state);
    entry["history"] = history;
    entries.push_back(std::move(entry));
  }
  require_matching_counts(exporter.counts(), expected);
  const auto &counts = exporter.counts();
  Json output{{"schema", "gtosd.hu_postflop_public_tree.v1"},
              {"treeFingerprint", tree.fingerprint},
              {"scope",
               "exact_public_betting_structure_without_board_combo_policy_frequency_or_ev"},
              {"entries", std::move(entries)},
              {"nodes", exporter.nodes()},
              {"stats",
               {{"representedNodes", counts.represented_nodes},
                {"decisionNodes", counts.decision_nodes},
                {"actionEdges", counts.action_edges},
                {"chanceFrontiers", counts.chance_frontiers},
                {"terminalFolds", counts.terminal_folds},
                {"terminalShowdowns", counts.terminal_showdowns},
                {"terminalAllInRunouts", counts.terminal_all_in_runouts},
                {"maximumDepth", counts.maximum_depth},
                {"maximumRaiseCount", counts.maximum_raise_count}}}};

  std::ofstream stream(output_path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot open postflop tree output");
  }
  stream << output.dump() << '\n';
  if (!stream) {
    throw std::runtime_error("cannot write postflop tree output");
  }
  std::cout << "HU_POSTFLOP_PUBLIC_TREE_EXPORT=PASS"
            << " output=" << output_path << " entries=" << histories.size()
            << " decision_nodes=" << counts.decision_nodes
            << " action_edges=" << counts.action_edges << '\n';
}

std::string parse_output_path(const int argc, char **argv) {
  if (argc == 1) {
    return {};
  }
  if (argc == 3 && std::string_view{argv[1]} == "--output") {
    return argv[2];
  }
  throw std::runtime_error("usage: gtosd_hu_preflop_tree [--output <postflop-tree.json>]");
}

int run(const int argc, char **argv) {
  const auto output_path = parse_output_path(argc, argv);
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
  if (!tree) {
    throw std::runtime_error(gtosd::hu_preflop_error_name(tree.error()));
  }
  const auto postflop = gtosd::analyze_hu_postflop_public_skeleton(tree.value());
  if (!postflop) {
    throw std::runtime_error(gtosd::hu_preflop_error_name(postflop.error()));
  }
  const auto &pre = tree.value().stats;
  const auto &post = postflop.value();
  const auto blueprint = gtosd::make_uniform_hu_preflop_blueprint(tree.value());
  const auto decomposition =
      blueprint ? gtosd::derive_hu_preflop_decomposition_plan(tree.value(), blueprint.value())
                : gtosd::Result<gtosd::HuPreflopDecompositionPlan,
                                gtosd::HuPreflopError>::failure(
                      gtosd::HuPreflopError::InvalidConfiguration);
  if (!decomposition) {
    throw std::runtime_error(gtosd::hu_preflop_error_name(decomposition.error()));
  }
  std::cout << "HU_PREFLOP_TREE=PASS"
            << " fingerprint=" << tree.value().fingerprint << " preflop_nodes=" << pre.node_count
            << " preflop_decisions=" << pre.decision_nodes
            << " postflop_entries=" << pre.postflop_entries
            << " preflop_folds=" << pre.terminal_folds << " preflop_allins=" << pre.terminal_all_ins
            << '\n';
  std::cout << "HU_POSTFLOP_PUBLIC_SKELETON"
            << " represented_nodes=" << post.represented_nodes
            << " decision_nodes=" << post.decision_nodes << " action_edges=" << post.action_edges
            << " chance_frontiers=" << post.chance_frontiers
            << " terminal_folds=" << post.terminal_folds
            << " terminal_showdowns=" << post.terminal_showdowns
            << " terminal_allin_runouts=" << post.terminal_all_in_runouts
            << " memoized_states=" << post.memoized_states
            << " maximum_depth=" << post.maximum_subtree_depth
            << " maximum_raise_count=" << static_cast<unsigned>(post.maximum_observed_raise_count)
            << " natural_termination="
            << (post.natural_stack_termination_proven ? "PROVEN" : "NOT_PROVEN") << '\n';
  std::cout << "HU_PREFLOP_FLOP_DECOMPOSITION"
            << " fingerprint=" << decomposition.value().fingerprint
            << " public_flop_roots=" << decomposition.value().public_flop_roots
            << " canonical_flops=" << decomposition.value().canonical_flops
            << " canonical_public_flop_roots="
            << decomposition.value().canonical_public_flop_roots
            << " physical_deal_flop_histories="
            << decomposition.value().physical_deal_flop_histories
            << " postflop_probability=" << decomposition.value().postflop_entry_probability
            << " fold_probability=" << decomposition.value().terminal_fold_probability
            << " allin_probability=" << decomposition.value().terminal_all_in_probability
            << " total_probability=" << decomposition.value().total_probability
            << " reach_template_bytes=" << decomposition.value().bytes.reach_template_bytes
            << " one_flop_range_bytes="
            << decomposition.value().bytes.one_flop_conditioned_range_bytes
            << " one_resolver_boundary_bytes="
            << decomposition.value().bytes.one_resolver_boundary_bytes
            << " all_resolver_boundaries_bytes="
            << decomposition.value().bytes.all_resolver_boundaries_bytes
            << " both_players_boundary_bytes="
            << decomposition.value().bytes.both_players_boundary_bytes
            << " fully_materialized_range_bytes="
            << decomposition.value().bytes.fully_materialized_range_bytes << '\n';
  if (!output_path.empty()) {
    export_postflop_tree(tree.value(), post, output_path);
  }
  return post.natural_stack_termination_proven ? 0 : 2;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "HU_PREFLOP_TREE=FAIL reason=" << error.what() << '\n';
    return 1;
  }
}
