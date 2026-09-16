#include "preflop_blueprint_test_support.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/chart_export.hpp"
#include "gtosd/preflop_blueprint/comparator.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/policy_query.hpp"
#include "gtosd/preflop_blueprint/postflop_tree_export.hpp"
#include "gtosd/preflop_blueprint/query_worker.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pb_test;
using Json = nlohmann::json;

pb::BucketPolicy random_policy(const pb::CompiledGame &game, const Resources &resources,
                               const std::uint64_t seed) {
  const auto layout = pb::layout_state(game, resources.flop->capacity(),
                                       resources.turn->capacity(), resources.river->capacity());
  pb::BucketPolicy policy(game, layout);
  ca::DeterministicRandom random(seed);
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row = 0; row < rows; ++row) {
      auto probabilities = policy.row(node.id, row);
      double total = 0.0;
      for (auto &probability : probabilities) {
        probability = 0.05 + random.uniform_unit();
        total += probability;
      }
      for (auto &probability : probabilities) {
        probability /= total;
      }
    }
  }
  return policy;
}

pb::QueryTables query_tables(const Resources &resources) {
  pb::QueryTables tables;
  tables.catalog = &resources.catalog.value();
  tables.flop = &resources.flop.value();
  tables.turn = &resources.turn.value();
  tables.river = &resources.river.value();
  return tables;
}

pb::BestResponseResources response_resources(const Resources &resources) {
  pb::BestResponseResources view;
  view.ranks = &resources.ranks.value();
  view.all_in = &resources.all_in.value();
  view.catalog = &resources.catalog.value();
  view.flop = &resources.flop.value();
  view.turn = &resources.turn.value();
  view.river = &resources.river.value();
  return view;
}

void test_action_labels() {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
  require(game.has_value(), "HU10 full compiles");
  std::size_t decisions = 0U;
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    ++decisions;
    const auto labels = pb::edge_labels(game.value(), node.id);
    require(labels.size() == node.action_count, "one label per edge");
    std::set<std::string> unique(labels.begin(), labels.end());
    require(unique.size() == labels.size(), "labels are unique within a node");
    for (const auto &label : labels) {
      require(label == "fold" || label == "check" || label == "call" || label == "all_in" ||
                  label.rfind("raise_", 0) == 0 || label.rfind("bet_", 0) == 0,
              "label vocabulary");
    }
  }
  require(pb::node_path_id(game.value(), game.value().root()) == "CO", "root id is the position");
  const auto preflop = pb::preflop_decision_nodes(game.value());
  require(!preflop.empty() && preflop.front() == game.value().root(), "preflop nodes start at the root");
  const auto root_labels = pb::edge_labels(game.value(), game.value().root());
  require(std::find(root_labels.begin(), root_labels.end(), "fold") != root_labels.end(),
          "root offers fold");
  std::set<std::string> ids;
  for (const auto node : preflop) {
    require(game.value().nodes()[node].street == gtosd::Street::Preflop, "preflop street");
    const auto id = pb::node_path_id(game.value(), node);
    require(ids.insert(id).second, "preflop node ids are unique");
    const auto steps = pb::history_steps(game.value(), node);
    require(steps.size() == pb::path_edges(game.value(), node).size(), "one step per preflop edge");
    if (node != game.value().root()) {
      require(steps.front().player == "CO", "the first step is the CO");
      require(id.rfind("CO_", 0) == 0, "ids start with the root position");
    }
  }
  std::cout << "action labels: " << decisions << " decision nodes, " << preflop.size()
            << " preflop nodes, root actions";
  for (const auto &label : root_labels) {
    std::cout << " " << label;
  }
  std::cout << ", " << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// Random walks: the query answers with the row of the board context and the
// probabilities of the policy; error cases.
void test_query(const Resources &resources) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
  require(game.has_value(), "HU10 full compiles");
  const auto policy = random_policy(game.value(), resources, 5U);
  const auto tables = query_tables(resources);
  pb::AbstractionTables abstraction;
  abstraction.catalog = &resources.catalog.value();
  abstraction.flop = &resources.flop.value();
  abstraction.turn = &resources.turn.value();
  abstraction.river = &resources.river.value();
  ca::DeterministicRandom random(77U);
  std::array<std::size_t, 4> per_street{};
  for (int walk = 0; walk < 160; ++walk) {
    // Random board and disjoint hand.
    std::array<std::uint8_t, 36> deck{};
    for (std::uint8_t card = 0; card < 36U; ++card) {
      deck[card] = card;
    }
    for (std::size_t index = 0; index < 7U; ++index) {
      const auto pick = index + static_cast<std::size_t>(random.uniform_unit() * static_cast<double>(36U - index));
      std::swap(deck[index], deck[std::min<std::size_t>(pick, 35U)]);
    }
    ca::BoardHistory history;
    history.flop = {gtosd::CardId::from_index(deck[0]).value(), gtosd::CardId::from_index(deck[1]).value(),
                    gtosd::CardId::from_index(deck[2]).value()};
    std::sort(history.flop.begin(), history.flop.end());
    history.turn = gtosd::CardId::from_index(deck[3]).value();
    history.river = gtosd::CardId::from_index(deck[4]).value();
    const std::array<gtosd::CardId, 2> hand{gtosd::CardId::from_index(deck[5]).value(),
                                            gtosd::CardId::from_index(deck[6]).value()};
    // Random path to a decision node.
    pb::QueryRequest request;
    request.hand = hand;
    request.board = {history.flop[0], history.flop[1], history.flop[2], history.turn, history.river};
    auto node = game.value().root();
    const auto target = static_cast<gtosd::Street>(walk % 4);
    for (int step = 0; step < 24; ++step) {
      const auto &entry = game.value().nodes()[node];
      if (entry.kind != pb::NodeKind::Decision) {
        break;
      }
      if (entry.street == target && (step > 0 || random.uniform_unit() < 0.3) &&
          random.uniform_unit() < 0.6) {
        break;
      }
      const auto labels = pb::edge_labels(game.value(), node);
      // Passive actions most of the time so that later streets are reached.
      std::vector<std::size_t> passive;
      std::vector<std::size_t> any;
      for (std::size_t index = 0; index < labels.size(); ++index) {
        any.push_back(index);
        if (labels[index] == "call" || labels[index] == "check") {
          passive.push_back(index);
        }
      }
      const auto &pool = (!passive.empty() && random.uniform_unit() < 0.7) ? passive : any;
      const auto choice = pool[std::min<std::size_t>(
          static_cast<std::size_t>(random.uniform_unit() * static_cast<double>(pool.size())),
          pool.size() - 1U)];
      request.actions.push_back(labels[choice]);
      node = game.value().edges_of(node)[choice].child;
      while (game.value().nodes()[node].kind == pb::NodeKind::Chance) {
        node = game.value().edges_of(node)[0].child;
      }
    }
    const auto result = pb::query_policy(game.value(), policy, tables, request);
    const auto &entry = game.value().nodes()[node];
    if (entry.kind != pb::NodeKind::Decision) {
      require(!result.has_value() && result.error() == pb::QueryError::TerminalReached,
              "terminal history is rejected");
      continue;
    }
    require(result.has_value(), "query succeeds on a legal history");
    require(result.value().node == node, "query reaches the walked node");
    require(result.value().node_id == pb::node_path_id(game.value(), node), "query node id");
    const auto context = pb::BoardContext::build(history, resources.ranks.value(), &abstraction);
    require(context.has_value(), "board context builds");
    const auto hand_index = context.value().hand_index(ca::combo_index(hand[0], hand[1]));
    require(hand_index != pb::no_hand, "hand is live on the board");
    const auto expected_row = context.value().row(entry.street, hand_index);
    require(result.value().row == expected_row, "query row equals the board context row");
    const auto probabilities = policy.row(node, expected_row);
    require(result.value().actions.size() == probabilities.size(), "one probability per action");
    for (std::size_t action = 0; action < probabilities.size(); ++action) {
      require(result.value().actions[action].probability == probabilities[action],
              "query probabilities equal the policy row");
    }
    ++per_street[static_cast<std::size_t>(entry.street)];
  }
  require(per_street[0] > 0U && per_street[1] > 0U && per_street[2] > 0U && per_street[3] > 0U,
          "walks reached every street");
  // Error cases.
  pb::QueryRequest bad;
  bad.hand = {card("As"), card("Ks")};
  bad.actions = {"raise_99"};
  require(!pb::query_policy(game.value(), policy, tables, bad).has_value() &&
              pb::query_policy(game.value(), policy, tables, bad).error() == pb::QueryError::UnknownAction,
          "unknown action is rejected");
  pb::QueryRequest on_board;
  on_board.hand = {card("As"), card("Ks")};
  on_board.actions = {pb::edge_labels(game.value(), game.value().root())[1], "call"};
  on_board.board = {card("As"), card("7d"), card("8c")};
  const auto on_board_result = pb::query_policy(game.value(), policy, tables, on_board);
  require(!on_board_result.has_value() &&
              (on_board_result.error() == pb::QueryError::HandOnBoard ||
               on_board_result.error() == pb::QueryError::UnknownAction),
          "hand on the board is rejected");
  pb::QueryRequest missing;
  missing.hand = {card("As"), card("Ks")};
  missing.actions = on_board.actions;
  const auto missing_result = pb::query_policy(game.value(), policy, tables, missing);
  require(!missing_result.has_value() &&
              (missing_result.error() == pb::QueryError::MissingBoard ||
               missing_result.error() == pb::QueryError::UnknownAction),
          "missing board is rejected");
  std::cout << "query: walks per street " << per_street[0] << "/" << per_street[1] << "/"
            << per_street[2] << "/" << per_street[3] << ", "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// Chart export: structure, EV consistency at the root, badges, round trip.
void test_chart_export(const Resources &resources, const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  auto config = resources.config();
  config.batch_boards = 4U;
  config.threads = 4U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
  require(trainer.has_value(), "trainer creates");
  for (int iteration = 0; iteration < 3; ++iteration) {
    require(trainer.value()->iterate().has_value(), "iteration succeeds");
  }
  const auto policy = trainer.value()->average_policy();
  const auto view = response_resources(resources);
  pb::ChartExportOptions options;
  options.threads = 4U;
  options.flops = 3U;
  options.iterations = 3U;
  options.abstraction = "test";
  const auto chart = pb::export_chart(game.value(), policy, view, options);
  require(chart.has_value(), "export succeeds");
  require(chart.value().badge == "ESTIMATED", "badge without certificate");
  const auto path = scratch / "chart_hu10_reduced.json";
  require(pb::write_text_atomically(path, chart.value().json).has_value(), "chart writes");
  const auto json = Json::parse(read_file(path));
  require(json["schema"] == "gtosd.preflop_blueprint_chart.v1", "chart schema");
  require(json["tree_fingerprint"] == game.value().fingerprint(), "chart tree fingerprint");
  require(json["fingerprints"]["policy"] == pb::policy_fingerprint(policy), "chart policy fingerprint");
  require(json["status"]["badge"] == "ESTIMATED" && json["status"]["exploitability"]["exact"] == false,
          "status block");
  require(json["checksum"].get<std::string>().rfind("fnv1a64:", 0) == 0, "checksum present");
  const auto preflop = pb::preflop_decision_nodes(game.value());
  require(json["preflop_nodes"].size() == preflop.size(), "one export node per preflop decision node");
  for (const auto node : preflop) {
    const auto id = pb::node_path_id(game.value(), node);
    require(json["preflop_nodes"].contains(id), "node id present");
    const auto &exported = json["preflop_nodes"][id];
    require(exported["strategy"].size() == 81U && exported["action_ev"].size() == 81U, "81 rows");
    require(exported["history"].size() == pb::history_steps(game.value(), node).size(), "history length");
    const auto labels = pb::edge_labels(game.value(), node);
    for (std::uint8_t hand_class = 0; hand_class < 81U; ++hand_class) {
      const auto name = gtosd::class_name(hand_class);
      const auto &row = exported["strategy"][name];
      double total = 0.0;
      for (const auto &label : labels) {
        require(row.contains(label), "strategy row has every action");
        total += row[label].get<double>();
        const auto &estimate = exported["action_ev"][name][label];
        require(estimate.contains("ev_ante") && estimate.contains("standard_error_ante") &&
                    estimate["samples"] == options.flops,
                "action EV fields");
      }
      require(std::abs(total - 1.0) <= 1e-9, "frequencies sum to one");
      const auto policy_row = policy.row(node, hand_class);
      for (std::size_t action = 0; action < labels.size(); ++action) {
        require(row[labels[action]].get<double>() == policy_row[action], "exported frequency equals the policy");
      }
    }
  }
  // Root EV consistency: mass-weighted, frequency-weighted class action EVs
  // equal the game value of the root actor.
  double reconstructed = 0.0;
  const auto root_id = pb::node_path_id(game.value(), game.value().root());
  const auto root_labels = pb::edge_labels(game.value(), game.value().root());
  for (std::uint8_t hand_class = 0; hand_class < 81U; ++hand_class) {
    const auto name = gtosd::class_name(hand_class);
    double class_value = 0.0;
    for (const auto &label : root_labels) {
      class_value += json["preflop_nodes"][root_id]["strategy"][name][label].get<double>() *
                     json["preflop_nodes"][root_id]["action_ev"][name][label]["ev_ante"].get<double>();
    }
    reconstructed += static_cast<double>(gtosd::class_mass(hand_class)) / 630.0 * class_value;
  }
  require(close(reconstructed, json["root_ev_ante"].get<double>(), 1e-9),
          "root action EVs reconstruct the root EV");
  require(close(json["root_ev_ante"].get<double>(), chart.value().estimate.ev[0], 1e-12), "root EV field");

  // Certificate badge: matching exact certificate -> CERTIFIED_EXACT; mismatch ignored.
  pb::Certificate certificate;
  certificate.exact = true;
  certificate.policy_fingerprint = pb::policy_fingerprint(policy);
  certificate.tree_fingerprint = game.value().fingerprint();
  certificate.report.max_gain = 0.004;
  certificate.report.max_gain_lower = 0.001;
  certificate.report.nashconv = 0.007;
  certificate.flops = 573U;
  certificate.physical_flops = 7140U;
  certificate.boards = 605088U;
  options.certificate = &certificate;
  const auto certified = pb::export_chart(game.value(), policy, view, options);
  require(certified.has_value() && certified.value().badge == "CERTIFIED_EXACT", "certified badge");
  const auto certified_json = Json::parse(certified.value().json);
  require(certified_json["status"]["exploitability"]["exact"] == true &&
              close(certified_json["status"]["exploitability"]["max_gain_antes"].get<double>(), 0.004, 1e-12),
          "certified status carries the exact value");
  certificate.policy_fingerprint = "fnv1a64:0000000000000000";
  const auto mismatched = pb::export_chart(game.value(), policy, view, options);
  require(mismatched.has_value() && mismatched.value().badge == "ESTIMATED", "mismatched certificate ignored");
  require(Json::parse(mismatched.value().json)["status"]["exploitability"].contains("certificate_ignored"),
          "mismatch is reported");

  // Query before and after the policy round trip through the file.
  const auto policy_path = scratch / "chart_policy.bin";
  require(pb::save_policy(policy_path, game.value(), policy, "export test").has_value(), "policy saves");
  const auto loaded = pb::load_policy(policy_path, game.value());
  require(loaded.has_value(), "policy loads");
  pb::QueryRequest request;
  request.hand = {card("As"), card("Kd")};
  request.actions = {root_labels[1]};
  const auto before = pb::query_policy(game.value(), policy, query_tables(resources), request);
  const auto after = pb::query_policy(game.value(), *loaded.value(), query_tables(resources), request);
  require(before.has_value() && after.has_value(), "queries succeed");
  for (std::size_t action = 0; action < before.value().actions.size(); ++action) {
    require(before.value().actions[action].probability == after.value().actions[action].probability,
            "query is identical before and after the export");
  }
  std::cout << "chart export: " << chart.value().nodes << " nodes, " << chart.value().json.size()
            << " bytes, root EV " << chart.value().root_ev[0] << ", estimate " << chart.value().estimate.max_gain
            << " (lower " << chart.value().estimate.max_gain_lower << "), "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// Comparator verdicts, baseline distances and the reference parser.
void test_comparator(const std::filesystem::path &scratch) {
  const auto started = Clock::now();
  auto candidate = Json::parse(read_file(scratch / "chart_hu10_reduced.json"));
  pb::ComparisonThresholds thresholds;
  const auto verdict = [&](Json document, const double threshold) {
    pb::ComparisonThresholds limits;
    limits.max_gain_antes = threshold;
    const auto report = pb::compare_charts(document.dump(), nullptr, nullptr, limits);
    require(report.has_value(), "comparison succeeds");
    return report.value().status;
  };
  auto exact_high = candidate;
  exact_high["status"]["exploitability"]["exact"] = true;
  exact_high["status"]["exploitability"]["max_gain_antes"] = 0.5;
  exact_high["status"]["exploitability"]["max_gain_half_width_antes"] = 0.0;
  require(verdict(exact_high, 0.1) == "REJECTED", "exact candidate above the threshold is rejected");
  auto exact_low = candidate;
  exact_low["status"]["exploitability"]["exact"] = true;
  exact_low["status"]["exploitability"]["max_gain_antes"] = 0.004;
  exact_low["status"]["exploitability"]["max_gain_half_width_antes"] = 0.0;
  require(verdict(exact_low, 0.1) == "QUALIFIED", "exact candidate within the threshold qualifies");
  auto estimated = candidate;
  estimated["status"]["exploitability"]["exact"] = false;
  estimated["status"]["exploitability"]["max_gain_antes"] = 0.05;
  estimated["status"]["exploitability"]["max_gain_half_width_antes"] = 0.02;
  estimated["status"]["exploitability"]["max_gain_lower_antes"] = 0.001;
  require(verdict(estimated, 0.1) == "PROMISING", "estimate within the threshold is promising");
  require(verdict(estimated, 0.06) == "INCONCLUSIVE_ESTIMATE", "estimate above the threshold is inconclusive");
  estimated["status"]["exploitability"]["max_gain_lower_antes"] = 0.2;
  require(verdict(estimated, 0.1) == "REJECTED", "lower bound above the threshold is rejected");

  // Baseline: the candidate itself -> zero distances; another tree -> stale.
  const auto text = candidate.dump();
  const auto same = pb::compare_charts(text, &text, nullptr, thresholds);
  require(same.has_value(), "baseline comparison succeeds");
  const auto same_json = Json::parse(same.value().json);
  require(same_json["baseline"]["status"] == "COMPARED" &&
              same_json["baseline"]["nodes_compared"] == candidate["preflop_nodes"].size(),
          "all nodes compared");
  require(same_json["baseline"]["worst_weighted_mean_class_total_variation_percentage_points"].get<double>() == 0.0,
          "identical baseline has zero distance");
  auto other = candidate;
  other["tree_fingerprint"] = "fnv1a64:ffffffffffffffff";
  const auto other_text = other.dump();
  const auto stale = pb::compare_charts(text, &other_text, nullptr, thresholds);
  require(stale.has_value() && Json::parse(stale.value().json)["baseline"]["status"] == "STALE_TREE",
          "different tree is stale");

  // Reference in Monker range format built from the candidate root: zero distance.
  Json reference;
  reference["schema"] = "gtosd.hu_preflop_reference.v1";
  reference["id"] = "synthetic";
  Json strategy = Json::object();
  std::vector<std::string> actions;
  for (const auto &[action, value] : candidate["strategy"].begin().value().items()) {
    static_cast<void>(value);
    actions.push_back(action);
  }
  for (const auto &action : actions) {
    std::string range;
    for (std::uint8_t hand_class = 0; hand_class < 81U; ++hand_class) {
      const auto name = gtosd::class_name(hand_class);
      const double probability = candidate["strategy"][name][action].get<double>();
      if (probability <= 0.0) {
        continue;
      }
      char buffer[64];
      std::snprintf(buffer, sizeof(buffer), "%.10f", probability * 100.0);
      if (!range.empty()) {
        range += ",";
      }
      range += "[" + std::string(buffer) + "]" + name + "[/" + std::string(buffer) + "]";
    }
    strategy[action] = range;
  }
  reference["reference"] = {{"root_ev_ante", candidate["root_ev_ante"]}, {"strategy", strategy}};
  const auto reference_text = reference.dump();
  const auto compared = pb::compare_charts(text, nullptr, &reference_text, thresholds);
  require(compared.has_value(), "reference comparison succeeds");
  const auto compared_json = Json::parse(compared.value().json);
  require(compared_json["reference"]["status"] == "EXTERNAL_CONTRACT_INCOMPLETE", "reference status");
  require(compared_json["reference"]["comparison"] == "ROOT_COMPARED", "reference root compared");
  require(compared_json["reference"]["root"]["weighted_mean_class_total_variation_percentage_points"].get<double>() <= 1e-6,
          "synthetic reference has zero distance");
  const auto legacy_reference = read_file(std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "fixtures" /
                                          "hu_preflop_co40_reference_v1.json");
  const auto legacy = pb::compare_charts(text, nullptr, &legacy_reference, thresholds);
  require(legacy.has_value() && Json::parse(legacy.value().json)["reference"]["comparison"] == "ACTION_SET_MISMATCH",
          "CO40 reference against HU10 reports the action set mismatch");
  std::cout << "comparator: verdicts, baseline, reference parser, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}


// Postflop tree export (viewer schema) and query worker: structure, counts,
// exactness flags and the EV of the flop root against the evaluator.
void test_postflop_tree_and_worker(const Resources &resources) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_full_v1.json"));
  require(game.has_value(), "HU10 full compiles");
  const auto policy = random_policy(game.value(), resources, 9U);
  const auto tree = pb::export_postflop_tree(game.value());
  const auto json = Json::parse(tree.json);
  require(json["schema"] == "gtosd.hu_postflop_public_tree.v1", "tree schema");
  require(json["treeFingerprint"] == game.value().fingerprint(), "tree fingerprint");
  const auto entries = game.value().postflop_entries();
  require(json["entries"].size() == entries.size(), "one export entry per postflop entry");
  std::size_t decisions = 0U;
  std::size_t edges = 0U;
  for (const auto &node : game.value().nodes()) {
    if (node.postflop_entry != pb::no_entry && node.kind == pb::NodeKind::Decision) {
      ++decisions;
      edges += node.action_count;
    }
  }
  require(json["nodes"].size() == decisions && tree.stats.decision_nodes == decisions, "decision node count");
  require(tree.stats.action_edges == edges && json["stats"]["actionEdges"] == edges, "action edge count");
  for (std::size_t index = 0; index < json["nodes"].size(); ++index) {
    const auto &node = json["nodes"][index];
    require(node["id"] == index, "contiguous export ids");
    require(tree.export_id[node["compiledNode"].get<std::uint32_t>()] == index, "export id map");
    const std::string street = node["state"]["street"];
    require(street == "flop" || street == "turn" || street == "river", "postflop street");
    for (const auto &action : node["actions"]) {
      const std::string kind = action["target"]["kind"];
      require(kind == "decision" || kind == "chance" || kind == "terminal", "target kind");
    }
  }
  for (const auto &entry : json["entries"]) {
    require(entry["target"]["kind"] == "chance" && entry["target"]["deals"] == "flop", "entries deal the flop");
  }

  const auto view = response_resources(resources);
  const pb::QueryWorker worker(game.value(), policy, view, 4U);
  const auto entry = entries.front();
  const auto flop_root = game.value().edges_of(entry)[0].child;
  const auto board = std::vector<gtosd::CardId>{card("7s"), card("8d"), card("9c"), card("Kh"), card("Ac")};

  pb::QueryWorkerRequest request;
  request.entry_node = entry;
  request.board = board;
  request.samples_per_action = 16U;
  const auto flop = worker.query(request);
  require(flop.has_value(), "flop query succeeds");
  require(flop.value().node == flop_root && flop.value().street == gtosd::Street::Flop, "flop node");
  require(!flop.value().exact && flop.value().runouts == 16U, "flop EVs use the sampled runouts");
  require(flop.value().visible_board_cards == 3U, "flop shows three cards");
  std::size_t live_classes = 0U;
  for (const auto &row : flop.value().rows) {
    if (row.live_physical_combos == 0U) {
      continue;
    }
    ++live_classes;
    double total = 0.0;
    for (const auto &action : row.actions) {
      total += action.frequency;
      require(std::isfinite(action.ev_ante), "finite action EV");
    }
    require(std::abs(total - 1.0) <= 1e-9, "class frequencies sum to one");
  }
  require(live_classes == 81U, "every class has live combos on a three-card board");
  double range_total = 0.0;
  for (const auto value : flop.value().action_frequencies) {
    range_total += value;
  }
  require(std::abs(range_total - 1.0) <= 1e-9, "range frequencies sum to one");

  // Flop root with all runouts: the class strategy EV equals the evaluator's
  // entry values divided by the opponent mass, class by class.
  request.samples_per_action = 4096U;
  const auto exact_flop = worker.query(request);
  require(exact_flop.has_value() && exact_flop.value().exact && exact_flop.value().runouts == 1056U,
          "all runouts make the flop query exact");
  const auto evaluator = pb::BestResponseEvaluator::create(game.value(), policy, view);
  require(evaluator.has_value(), "evaluator creates");
  std::array<gtosd::CardId, 3> flop_cards{board[0], board[1], board[2]};
  std::sort(flop_cards.begin(), flop_cards.end());
  const auto group = pb::full_runouts(flop_cards);
  const auto values = evaluator.value().evaluate_flop(group);
  const auto hero = game.value().nodes()[flop_root].actor;
  const auto probe = evaluator.value().probe_node(group, flop_root, hero);
  require(values.has_value() && probe.has_value() && probe.value().found, "evaluator values and probe");
  const auto &entry_values = values.value().entry_values[hero][pb::average_mode][0];
  const auto &table = ca::combo_table();
  for (std::uint8_t hand_class = 0; hand_class < 81U; ++hand_class) {
    double numerator = 0.0;
    double mass = 0.0;
    for (std::uint16_t combo = 0; combo < 630U; ++combo) {
      if (table.hand_class[combo] != hand_class || values.value().compatible[combo] == 0U) {
        continue;
      }
      numerator += entry_values[combo];
      mass += probe.value().opponent_mass[combo];
    }
    if (mass <= 0.0) {
      continue;
    }
    require(close(exact_flop.value().rows[hand_class].strategy_ev_ante, numerator / mass, 1e-9),
            "worker class EV equals the evaluator at the flop root");
  }

  // Turn and river nodes through passive actions.
  auto node = flop_root;
  std::vector<std::uint32_t> indices;
  while (game.value().nodes()[node].street == gtosd::Street::Flop) {
    const auto labels = pb::edge_labels(game.value(), node);
    const auto passive = std::find_if(labels.begin(), labels.end(), [](const std::string &label) {
      return label == "check" || label == "call";
    });
    require(passive != labels.end(), "passive action exists");
    const auto choice = static_cast<std::uint32_t>(passive - labels.begin());
    indices.push_back(choice);
    node = game.value().edges_of(node)[choice].child;
    while (game.value().nodes()[node].kind == pb::NodeKind::Chance) {
      node = game.value().edges_of(node)[0].child;
    }
  }
  request.action_indices = indices;
  const auto turn = worker.query(request);
  require(turn.has_value() && turn.value().street == gtosd::Street::Turn, "turn query reaches the turn");
  require(turn.value().exact && turn.value().runouts == 32U && turn.value().visible_board_cards == 4U,
          "turn EVs enumerate the 32 rivers");
  while (game.value().nodes()[node].street == gtosd::Street::Turn) {
    const auto labels = pb::edge_labels(game.value(), node);
    const auto passive = std::find_if(labels.begin(), labels.end(), [](const std::string &label) {
      return label == "check" || label == "call";
    });
    const auto choice = static_cast<std::uint32_t>(passive - labels.begin());
    indices.push_back(choice);
    node = game.value().edges_of(node)[choice].child;
    while (game.value().nodes()[node].kind == pb::NodeKind::Chance) {
      node = game.value().edges_of(node)[0].child;
    }
  }
  request.action_indices = indices;
  const auto river = worker.query(request);
  require(river.has_value() && river.value().street == gtosd::Street::River, "river query reaches the river");
  require(river.value().exact && river.value().visible_board_cards == 5U, "river EVs are exact");
  std::size_t river_live = 0U;
  for (const auto &row : river.value().rows) {
    if (row.live_physical_combos > 0U) {
      ++river_live;
      double total = 0.0;
      for (const auto &action : row.actions) {
        total += action.frequency;
        require(std::isfinite(action.ev_ante), "finite river EV");
      }
      require(std::abs(total - 1.0) <= 1e-9, "river class frequencies sum to one");
    }
  }
  require(river_live > 60U, "most classes stay live on a five-card board");
  const auto response_json = Json::parse(pb::query_worker_response_json(river.value(), game.value(), board));
  require(response_json["rows"].size() == 81U && response_json["board"].size() == 5U, "response JSON layout");

  // Errors.
  pb::QueryWorkerRequest bad = request;
  bad.entry_node = flop_root;
  require(!worker.query(bad).has_value() && worker.query(bad).error() == pb::QueryWorkerError::InvalidEntry,
          "non-entry node rejected");
  pb::QueryWorkerRequest short_board = request;
  short_board.board = {card("7s"), card("8d"), card("9c")};
  require(!worker.query(short_board).has_value() &&
              worker.query(short_board).error() == pb::QueryWorkerError::MissingBoard,
          "river query without the river card rejected");
  std::cout << "postflop tree and worker: " << decisions << " decision nodes, " << edges
            << " edges, flop " << flop.value().seconds << " s, exact flop " << exact_flop.value().seconds
            << " s, turn " << turn.value().seconds << " s, river " << river.value().seconds << " s, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path scratch_dir = std::filesystem::temp_directory_path() / "gtosd_export_tests";
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--resources-dir") {
        resources_dir = std::filesystem::path(value);
      } else if (name == "--buckets-dir") {
        buckets_dir = std::filesystem::path(value);
      } else if (name == "--scratch-dir") {
        scratch_dir = std::filesystem::path(value);
      }
    }
    std::filesystem::create_directories(scratch_dir);
    const auto resources = load_resources(resources_dir, buckets_dir);
    std::cout << "resources " << (resources.loaded ? "loaded" : "built") << ", bucket tables "
              << (resources.buckets_loaded ? "loaded" : "synthetic") << "\n";
    test_action_labels();
    test_query(resources);
    test_chart_export(resources, scratch_dir);
    test_comparator(scratch_dir);
    test_postflop_tree_and_worker(resources);
    std::cout << "PREFLOP_BLUEPRINT_EXPORT_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_EXPORT_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
