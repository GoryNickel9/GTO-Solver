#include "gtosd/preflop/hu_preflop.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;

struct Arguments {
  std::vector<std::string> candidates;
  std::string output;
};

Arguments parse_arguments(const int argc, char **argv) {
  Arguments result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view token{argv[index]};
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value after " + std::string(token));
    }
    if (token == "--candidate") {
      result.candidates.emplace_back(argv[++index]);
    } else if (token == "--output") {
      result.output = argv[++index];
    } else {
      throw std::runtime_error("unknown argument " + std::string(token));
    }
  }
  if (result.candidates.empty() || result.output.empty()) {
    throw std::runtime_error(
        "usage: gtosd_hu_preflop_all_in_policy_audit --candidate <solve.json> [...] "
        "--output <audit.json>");
  }
  return result;
}

Json read_json(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open candidate " + path);
  }
  return Json::parse(input);
}

std::string player_id(const std::uint8_t player) {
  if (player == 0U) {
    return "CO";
  }
  if (player == 1U) {
    return "BTN";
  }
  throw std::runtime_error("invalid player");
}

std::string preflop_action_id(const gtosd::HuPreflopTree &tree,
                              const gtosd::HuPreflopNode &node,
                              const gtosd::Action &action) {
  switch (action.type) {
  case gtosd::ActionType::Fold:
    return "fold";
  case gtosd::ActionType::Check:
    return "check";
  case gtosd::ActionType::Call:
    return "call";
  case gtosd::ActionType::AllIn:
    return "all_in";
  case gtosd::ActionType::Bet:
  case gtosd::ActionType::Raise: {
    const auto ante_units = tree.config.ante.units();
    const auto target_units =
        node.state.committed_this_street[node.state.player_to_act].units() + action.amount.units();
    if (ante_units <= 0 || target_units <= 0 || (target_units * 2) % ante_units != 0) {
      throw std::runtime_error("preflop raise target is not an exact half-ante");
    }
    const auto half_antes = (target_units * 2) / ante_units;
    return half_antes % 2 == 0
               ? "raise_" + std::to_string(half_antes / 2)
               : "raise_" + std::to_string(half_antes / 2) + "_5";
  }
  }
  throw std::runtime_error("unknown action type");
}

struct CandidateBlueprint {
  gtosd::HuPreflopBlueprint blueprint;
  std::map<std::uint32_t, const Json *> source_nodes;
};

CandidateBlueprint candidate_blueprint(const gtosd::HuPreflopTree &tree,
                                       const Json &candidate) {
  if (candidate.at("schema") != "gtosd.hu_preflop_candidate.v1" ||
      candidate.at("tree_fingerprint") != tree.fingerprint ||
      !candidate.contains("preflop_nodes")) {
    throw std::runtime_error("candidate does not contain a compatible full preflop export");
  }

  CandidateBlueprint result;
  result.blueprint.tree_fingerprint = tree.fingerprint;
  result.blueprint.algorithm = candidate.at("algorithm").get<std::string>();
  result.blueprint.iterations = candidate.at("iterations").get<std::uint64_t>();
  for (const auto &[_, source] : candidate.at("preflop_nodes").items()) {
    const auto node_id = source.at("tree_node_id").get<std::uint32_t>();
    if (!result.source_nodes.emplace(node_id, &source).second) {
      throw std::runtime_error("candidate contains a duplicate preflop node");
    }
  }

  for (const auto &node : tree.nodes) {
    if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
      continue;
    }
    const auto found = result.source_nodes.find(node.id);
    if (found == result.source_nodes.end()) {
      throw std::runtime_error("candidate omits preflop node " + std::to_string(node.id));
    }
    const auto &source = *found->second;
    if (source.at("player").get<std::string>() != player_id(node.state.player_to_act)) {
      throw std::runtime_error("candidate player mismatch at preflop node " +
                               std::to_string(node.id));
    }

    gtosd::HuPreflopBlueprintDecision decision;
    decision.node_id = node.id;
    decision.player = node.state.player_to_act;
    decision.action_count = static_cast<std::uint8_t>(node.edges.size());
    for (std::size_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
      const auto hand_name = gtosd::class_name(static_cast<gtosd::HandClassId>(hand));
      const auto &row = source.at("strategy").at(hand_name);
      double total = 0.0;
      for (std::size_t action = 0U; action < node.edges.size(); ++action) {
        const auto action_name = preflop_action_id(tree, node, node.edges[action].action);
        const auto probability = row.at(action_name).get<double>();
        if (!std::isfinite(probability) || probability < 0.0) {
          throw std::runtime_error("candidate contains an invalid strategy probability");
        }
        decision.strategy[hand][action] = probability;
        total += probability;
      }
      if (std::abs(total - 1.0) > 1.0e-9) {
        throw std::runtime_error("candidate strategy row is not normalized");
      }
    }
    result.blueprint.decisions.push_back(std::move(decision));
  }
  result.blueprint.fingerprint = gtosd::fingerprint_hu_preflop_blueprint(result.blueprint);
  const auto valid = gtosd::validate_hu_preflop_blueprint(tree, result.blueprint);
  if (!valid) {
    throw std::runtime_error("candidate blueprint failed validation");
  }
  return result;
}

const gtosd::HuPreflopBlueprintDecision &
find_decision(const gtosd::HuPreflopBlueprint &blueprint, const std::uint32_t node_id) {
  const auto found = std::ranges::find_if(
      blueprint.decisions,
      [node_id](const auto &candidate) { return candidate.node_id == node_id; });
  if (found == blueprint.decisions.end()) {
    throw std::runtime_error("blueprint decision is missing");
  }
  return *found;
}

struct PathStep {
  std::uint32_t node_id{0U};
  std::size_t edge_index{0U};
};

bool find_path(const gtosd::HuPreflopTree &tree, const std::uint32_t current,
               const std::uint32_t target, std::vector<PathStep> &path) {
  if (current == target) {
    return true;
  }
  if (current >= tree.nodes.size()) {
    return false;
  }
  const auto &node = tree.nodes[current];
  for (std::size_t edge = 0U; edge < node.edges.size(); ++edge) {
    path.push_back({current, edge});
    if (find_path(tree, node.edges[edge].child, target, path)) {
      return true;
    }
    path.pop_back();
  }
  return false;
}

double own_sequence_reach(const gtosd::HuPreflopTree &tree,
                          const gtosd::HuPreflopBlueprint &blueprint,
                          const std::vector<PathStep> &path, const std::uint8_t player,
                          const std::size_t hand) {
  double reach = 1.0;
  for (const auto &step : path) {
    const auto &node = tree.nodes.at(step.node_id);
    if (node.state.player_to_act == player) {
      reach *= find_decision(blueprint, node.id).strategy.at(hand).at(step.edge_index);
    }
  }
  return reach;
}

Json sampled_estimate(const Json &source, const std::string &hand, const std::string &action) {
  if (!source.contains("action_ev") || !source.at("action_ev").contains(hand) ||
      !source.at("action_ev").at(hand).contains(action) ||
      source.at("action_ev").at(hand).at(action).is_null()) {
    return nullptr;
  }
  const auto &estimate = source.at("action_ev").at(hand).at(action);
  Json result{{"evAnte", estimate.at("ev_ante")},
              {"standardErrorAnte", estimate.at("standard_error_ante")},
              {"samples", estimate.at("samples")}};
  if (estimate.contains("effective_samples")) {
    result["effectiveSamples"] = estimate.at("effective_samples");
  }
  return result;
}

Json audit_candidate(const gtosd::HuPreflopTree &tree,
                     const gtosd::HuPreflopAllInBoardCatalog &catalog,
                     const gtosd::HuPreflopAllInEquityTable &equity_table,
                     const std::string &candidate_path, const Json &candidate) {
  const auto imported = candidate_blueprint(tree, candidate);
  const auto &blueprint = imported.blueprint;
  Json nodes = Json::array();
  std::uint64_t audited_rows = 0U;
  std::uint64_t material_inferior_rows = 0U;
  std::uint64_t material_rows_own_reach_1pct = 0U;
  std::uint64_t material_rows_own_reach_0_1pct = 0U;
  double total_local_loss = 0.0;
  double maximum_local_loss = 0.0;
  double maximum_realization_weighted_loss = 0.0;
  Json maximum_case = nullptr;
  Json maximum_realization_case = nullptr;
  constexpr double material_frequency = 0.05;
  constexpr double material_ev_gap_ante = 0.10;
  const std::string continuation = "exact_preflop_all_in_policy_audit_v1";

  for (const auto &node : tree.nodes) {
    if (node.kind != gtosd::HuPreflopNodeKind::Decision || node.edges.size() != 2U) {
      continue;
    }
    std::size_t call_index = node.edges.size();
    std::size_t fold_index = node.edges.size();
    for (std::size_t action = 0U; action < node.edges.size(); ++action) {
      if (node.edges[action].action.type == gtosd::ActionType::Call) {
        call_index = action;
      } else if (node.edges[action].action.type == gtosd::ActionType::Fold) {
        fold_index = action;
      }
    }
    if (call_index >= node.edges.size() || fold_index >= node.edges.size()) {
      continue;
    }
    const auto call_child = node.edges[call_index].child;
    const auto fold_child = node.edges[fold_index].child;
    if (tree.nodes.at(call_child).kind != gtosd::HuPreflopNodeKind::TerminalAllIn ||
        tree.nodes.at(fold_child).kind != gtosd::HuPreflopNodeKind::TerminalFold) {
      continue;
    }

    const auto call_values = gtosd::evaluate_hu_preflop_best_response_all_in_terminal(
        tree, blueprint, catalog, equity_table, call_child, continuation,
        node.state.player_to_act, blueprint.iterations);
    const auto fold_values = gtosd::evaluate_hu_preflop_best_response_fold_terminal(
        tree, blueprint, fold_child, continuation, node.state.player_to_act,
        blueprint.iterations);
    if (!call_values || !fold_values) {
      throw std::runtime_error("exact terminal evaluation failed at node " +
                               std::to_string(node.id));
    }

    const auto &decision = find_decision(blueprint, node.id);
    const auto &source = *imported.source_nodes.at(node.id);
    std::vector<PathStep> path;
    if (!find_path(tree, tree.root, node.id, path)) {
      throw std::runtime_error("cannot reconstruct path to node " + std::to_string(node.id));
    }
    Json rows = Json::object();
    for (std::size_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
      const auto hand_name = gtosd::class_name(static_cast<gtosd::HandClassId>(hand));
      const auto call_frequency = decision.strategy[hand][call_index];
      const auto fold_frequency = decision.strategy[hand][fold_index];
      const auto &call_value = call_values.value().values.at(hand);
      const auto &fold_value = fold_values.value().values.at(hand);
      if (!call_value.positive_reach || !fold_value.positive_reach) {
        throw std::runtime_error("zero exact reach at audited node " + std::to_string(node.id));
      }
      const auto call_ev = call_value.conditional_value_antes;
      const auto fold_ev = fold_value.conditional_value_antes;
      const auto delta = call_ev - fold_ev;
      const auto inferior_action = delta < 0.0 ? "call" : delta > 0.0 ? "fold" : "none";
      const auto inferior_frequency =
          delta < 0.0 ? call_frequency : delta > 0.0 ? fold_frequency : 0.0;
      const auto local_loss = std::abs(delta) * inferior_frequency;
      const auto own_reach =
          own_sequence_reach(tree, blueprint, path, node.state.player_to_act, hand);
      const auto realization_weighted_loss = own_reach * local_loss;
      const bool material = inferior_frequency >= material_frequency &&
                            std::abs(delta) >= material_ev_gap_ante;
      ++audited_rows;
      material_inferior_rows += material ? 1U : 0U;
      material_rows_own_reach_1pct += material && own_reach >= 0.01 ? 1U : 0U;
      material_rows_own_reach_0_1pct += material && own_reach >= 0.001 ? 1U : 0U;
      total_local_loss += local_loss;
      if (local_loss > maximum_local_loss) {
        maximum_local_loss = local_loss;
        maximum_case = {{"nodeId", node.id},
                        {"hand", hand_name},
                        {"inferiorAction", inferior_action},
                        {"inferiorFrequency", inferior_frequency},
                        {"evGapAnte", std::abs(delta)},
                        {"localExpectedLossAnte", local_loss}};
      }
      if (realization_weighted_loss > maximum_realization_weighted_loss) {
        maximum_realization_weighted_loss = realization_weighted_loss;
        maximum_realization_case = {{"nodeId", node.id},
                                   {"hand", hand_name},
                                   {"ownSequenceReach", own_reach},
                                   {"inferiorAction", inferior_action},
                                   {"inferiorFrequency", inferior_frequency},
                                   {"evGapAnte", std::abs(delta)},
                                   {"realizationWeightedLocalLossAnte",
                                    realization_weighted_loss}};
      }
      rows[hand_name] = {
          {"callFrequency", call_frequency},
          {"foldFrequency", fold_frequency},
          {"exactCallEvAnte", call_ev},
          {"exactFoldEvAnte", fold_ev},
          {"exactCallMinusFoldAnte", delta},
          {"exactStrategyEvAnte", call_frequency * call_ev + fold_frequency * fold_ev},
          {"inferiorAction", inferior_action},
          {"inferiorFrequency", inferior_frequency},
          {"localExpectedLossAnte", local_loss},
          {"ownSequenceReach", own_reach},
          {"realizationWeightedLocalLossAnte", realization_weighted_loss},
          {"material", material},
          {"reportedCallEstimate", sampled_estimate(source, hand_name, "call")},
          {"reportedFoldEstimate", sampled_estimate(source, hand_name, "fold")}};
    }
    nodes.push_back({{"nodeId", node.id},
                     {"sourceNodeId", source.at("id")},
                     {"player", player_id(node.state.player_to_act)},
                     {"history", source.at("history")},
                     {"callTerminalNode", call_child},
                     {"foldTerminalNode", fold_child},
                     {"rows", std::move(rows)}});
  }

  return {{"source", candidate_path},
          {"algorithm", candidate.at("algorithm")},
          {"abstraction", candidate.at("abstraction")},
          {"iterations", candidate.at("iterations")},
          {"seed", candidate.at("seed")},
          {"blueprintFingerprint", blueprint.fingerprint},
          {"summary",
           {{"auditedNodes", nodes.size()},
            {"auditedRows", audited_rows},
            {"materialInferiorRows", material_inferior_rows},
            {"materialRowsOwnReachAtLeastOnePercent", material_rows_own_reach_1pct},
            {"materialRowsOwnReachAtLeastPointOnePercent", material_rows_own_reach_0_1pct},
            {"materialFrequencyThreshold", material_frequency},
            {"materialEvGapAnteThreshold", material_ev_gap_ante},
            {"meanLocalExpectedLossAnte",
             audited_rows > 0U ? total_local_loss / static_cast<double>(audited_rows) : 0.0},
            {"maximumLocalExpectedLossAnte", maximum_local_loss},
            {"maximumCase", std::move(maximum_case)},
            {"maximumRealizationWeightedLocalLossAnte", maximum_realization_weighted_loss},
            {"maximumRealizationCase", std::move(maximum_realization_case)}}},
          {"nodes", std::move(nodes)}};
}

Json serialize_class_equities(const gtosd::HuPreflopAllInEquityTable &table) {
  Json rows = Json::array();
  for (std::size_t responder = 0U; responder < gtosd::hu_preflop_hand_class_count; ++responder) {
    for (std::size_t opponent = 0U; opponent < gtosd::hu_preflop_hand_class_count; ++opponent) {
      const auto &matchup =
          table.matchups.at(responder * gtosd::hu_preflop_hand_class_count + opponent);
      const auto total = matchup.responding_player_wins + matchup.ties +
                         matchup.responding_player_losses;
      if (total == 0U) {
        throw std::runtime_error("exact all-in table contains an empty class matchup");
      }
      const auto equity =
          (static_cast<long double>(matchup.responding_player_wins) +
           0.5L * static_cast<long double>(matchup.ties)) /
          static_cast<long double>(total);
      rows.push_back(
          {{"responder", gtosd::class_name(static_cast<gtosd::HandClassId>(responder))},
           {"opponent", gtosd::class_name(static_cast<gtosd::HandClassId>(opponent))},
           {"wins", matchup.responding_player_wins},
           {"ties", matchup.ties},
           {"losses", matchup.responding_player_losses},
           {"equity", static_cast<double>(equity)}});
    }
  }
  return rows;
}

int run(const int argc, char **argv) {
  const auto arguments = parse_arguments(argc, argv);
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
  if (!tree) {
    throw std::runtime_error(gtosd::hu_preflop_error_name(tree.error()));
  }
  const auto catalog = gtosd::build_hu_preflop_all_in_board_catalog();
  if (!catalog) {
    throw std::runtime_error("cannot build exact all-in board catalog");
  }
  const auto equity_table = gtosd::build_hu_preflop_all_in_equity_table(catalog.value());
  if (!equity_table) {
    throw std::runtime_error("cannot build exact all-in equity table");
  }

  Json candidates = Json::array();
  for (const auto &candidate_path : arguments.candidates) {
    const auto candidate = read_json(candidate_path);
    candidates.push_back(audit_candidate(tree.value(), catalog.value(), equity_table.value(),
                                         candidate_path, candidate));
  }
  Json output{{"schema", "gtosd.hu_preflop_all_in_policy_audit.v1"},
              {"treeFingerprint", tree.value().fingerprint},
              {"exactEquityTableFingerprint", equity_table.value().fingerprint},
              {"canonicalBoards", catalog.value().boards.size()},
              {"physicalUnorderedBoards", catalog.value().physical_unordered_boards},
              {"orderedPublicRunoutsPerPrivateDeal",
               equity_table.value().ordered_public_runouts_per_private_deal},
              {"matchupOutcomes", equity_table.value().matchup_outcome_count},
              {"classEquities", serialize_class_equities(equity_table.value())},
              {"candidates", std::move(candidates)}};
  std::ofstream stream(arguments.output, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot open output " + arguments.output);
  }
  stream << output.dump(2) << '\n';
  if (!stream) {
    throw std::runtime_error("cannot write output " + arguments.output);
  }
  std::cout << "HU_PREFLOP_ALL_IN_POLICY_AUDIT=PASS"
            << " candidates=" << arguments.candidates.size()
            << " exact_equity_table=" << equity_table.value().fingerprint
            << " output=" << arguments.output << '\n';
  return 0;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "HU_PREFLOP_ALL_IN_POLICY_AUDIT=FAIL reason=" << error.what() << '\n';
    return 1;
  }
}
