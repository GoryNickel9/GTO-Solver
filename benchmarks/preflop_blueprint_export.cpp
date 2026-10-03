// Chart export and policy query of the preflop blueprint (roadmap P8).
//
//   export: --config --resources-dir --buckets-dir --policy [--certificate cert.json]
//           --eval-flops M --threads T --output chart.json [--iterations N --algorithm ...]
//   query:  ... --query-history "raise_6,call" --query-hand AhKs [--query-board 7s8d9c]
//           prints the action distribution as JSON.
//   tree:   ... --postflop-tree tree.json   writes the public postflop tree for the viewer.
//   serve:  ... --serve                     persistent worker: one JSON request per stdin line
//           ({entryNode, actionIndices, board, samplesPerAction, seed}), one JSON response per line.
#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/certifier.hpp"
#include "gtosd/preflop_blueprint/chart_export.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/policy_query.hpp"
#include "gtosd/preflop_blueprint/postflop_tree_export.hpp"
#include "gtosd/preflop_blueprint/query_worker.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
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
using Json = nlohmann::json;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint64_t parse_unsigned(const std::string_view text) {
  return std::stoull(std::string(text));
}

std::vector<std::string> split(const std::string_view text, const char separator) {
  std::vector<std::string> parts;
  std::size_t begin = 0U;
  while (begin <= text.size()) {
    const auto end = text.find(separator, begin);
    const auto part = text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
    if (!part.empty()) {
      parts.emplace_back(part);
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
  return parts;
}

std::vector<gtosd::CardId> parse_cards(const std::string_view text) {
  std::vector<gtosd::CardId> cards;
  for (std::size_t index = 0; index + 2U <= text.size(); index += 2U) {
    const auto card = gtosd::parse_card(text.substr(index, 2U));
    if (!card) {
      throw std::runtime_error("invalid card in " + std::string(text));
    }
    cards.push_back(card.value());
  }
  return cards;
}

// Certificate JSON -> the fields the export needs.
pb::Certificate load_certificate(const std::filesystem::path &path) {
  const auto json = Json::parse(read_file(path));
  if (json.value("schema", "") != "gtosd.preflop_blueprint_certificate.v1") {
    throw std::runtime_error("certificate schema mismatch");
  }
  // A street-restricted certificate measures part of the exploitability only (a lower
  // bound), so it must never badge a policy as certified.
  if (json.contains("deviation_from")) {
    throw std::runtime_error("a street-restricted certificate does not certify a policy");
  }
  pb::Certificate certificate;
  certificate.exact = json.value("exact", false);
  certificate.partial = json.value("partial", false);
  certificate.sampled = json.value("sampled", false);
  certificate.flops = json.value("flops", 0U);
  certificate.physical_flops = json.value("physical_flops", 0U);
  certificate.boards = json.value("boards", 0U);
  certificate.tree_fingerprint = json.value("tree_fingerprint", "");
  certificate.policy_fingerprint = json.value("policy_fingerprint", "");
  certificate.report.max_gain = json.value("max_gain", 0.0);
  certificate.report.max_gain_lower = json.value("max_gain_lower", 0.0);
  certificate.report.nashconv = json.value("nashconv", 0.0);
  for (std::size_t player = 0; player < 2U; ++player) {
    certificate.report.ev[player] = json["ev"][player].get<double>();
    certificate.report.gain[player] = json["gain"][player].get<double>();
    certificate.report.best_response[player] = json["best_response"][player].get<double>();
  }
  return certificate;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path config_path;
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path policy_path;
    std::filesystem::path certificate_path;
    std::filesystem::path output_path;
    std::string query_history;
    std::string query_hand;
    std::string query_board;
    std::filesystem::path postflop_tree_path;
    bool serve = false;
    pb::ChartExportOptions options;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--serve") {
        serve = true;
        continue;
      }
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{name});
      }
      const std::string_view value = argv[++index];
      if (name == "--config") {
        config_path = std::filesystem::path(value);
      } else if (name == "--resources-dir") {
        resources_dir = std::filesystem::path(value);
      } else if (name == "--buckets-dir") {
        buckets_dir = std::filesystem::path(value);
      } else if (name == "--policy") {
        policy_path = std::filesystem::path(value);
      } else if (name == "--certificate") {
        certificate_path = std::filesystem::path(value);
      } else if (name == "--output") {
        output_path = std::filesystem::path(value);
      } else if (name == "--eval-flops") {
        options.flops = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--eval-seed") {
        options.seed = parse_unsigned(value);
      } else if (name == "--threads") {
        options.threads = static_cast<unsigned>(parse_unsigned(value));
      } else if (name == "--iterations") {
        options.iterations = parse_unsigned(value);
      } else if (name == "--algorithm") {
        options.algorithm = std::string(value);
      } else if (name == "--query-history") {
        query_history = std::string(value);
      } else if (name == "--query-hand") {
        query_hand = std::string(value);
      } else if (name == "--query-board") {
        query_board = std::string(value);
      } else if (name == "--postflop-tree") {
        postflop_tree_path = std::filesystem::path(value);
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    if (config_path.empty() || resources_dir.empty() || buckets_dir.empty() || policy_path.empty()) {
      throw std::runtime_error("--config, --resources-dir, --buckets-dir and --policy are required");
    }
    const auto started = Clock::now();
    const auto game_config = pb::parse_game_config_json(read_file(config_path));
    if (!game_config) {
      throw std::runtime_error(std::string("configuration rejected: ") +
                               pb::config_error_name(game_config.error()));
    }
    const auto compiled = pb::CompiledGame::compile(game_config.value());
    if (!compiled) {
      throw std::runtime_error("compile failed");
    }
    auto flop = ca::BucketTable::load(buckets_dir / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(buckets_dir / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(buckets_dir / "river_buckets_v1.bin");
    if (!flop || !turn || !river) {
      throw std::runtime_error("bucket tables missing");
    }
    const auto catalog = ca::BoardCatalog::build();
    pb::PolicyFileInfo info;
    auto loaded = pb::load_policy(policy_path, compiled.value(), &info);
    if (!loaded) {
      throw std::runtime_error(std::string("policy rejected: ") +
                               pb::policy_file_error_name(loaded.error()));
    }
    const auto &policy = *loaded.value();
    options.policy_source = info.source;
    options.abstraction = "kmeans_buckets_" + std::to_string(info.flop_capacity) + "_" +
                          std::to_string(info.turn_capacity) + "_" +
                          std::to_string(info.river_capacity);

    if (!postflop_tree_path.empty()) {
      const auto tree = pb::export_postflop_tree(compiled.value());
      const auto written = pb::write_text_atomically(postflop_tree_path, tree.json);
      if (!written) {
        throw std::runtime_error("cannot write " + postflop_tree_path.string());
      }
      std::cout << "{\"event\": \"postflop_tree\", \"entries\": " << compiled.value().postflop_entries().size()
                << ", \"decision_nodes\": " << tree.stats.decision_nodes
                << ", \"action_edges\": " << tree.stats.action_edges
                << ", \"terminal_folds\": " << tree.stats.terminal_folds
                << ", \"terminal_showdowns\": " << tree.stats.terminal_showdowns
                << ", \"terminal_all_in_runouts\": " << tree.stats.terminal_all_in_runouts
                << ", \"bytes\": " << tree.json.size() << "}\n";
      if (output_path.empty() && query_hand.empty() && !serve) {
        std::cout << "PREFLOP_BLUEPRINT_EXPORT=POSTFLOP_TREE\n";
        return 0;
      }
    }

    if (serve) {
      auto ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
      auto all_in = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
      if (!ranks || !all_in) {
        throw std::runtime_error("resources missing");
      }
      pb::BestResponseResources resources;
      resources.ranks = &ranks.value();
      resources.all_in = &all_in.value();
      resources.catalog = &catalog;
      resources.flop = &flop.value();
      resources.turn = &turn.value();
      resources.river = &river.value();
      const pb::QueryWorker worker(compiled.value(), policy, resources, options.threads);
      Json ready;
      ready["status"] = "ready";
      ready["backend"] = "preflop_blueprint";
      ready["configId"] = game_config.value().id;
      ready["treeFingerprint"] = compiled.value().fingerprint();
      ready["policyFingerprint"] = pb::policy_fingerprint(policy);
      ready["policySource"] = info.source;
      ready["iterations"] = options.iterations;
      ready["capacities"] = {info.flop_capacity, info.turn_capacity, info.river_capacity};
      std::cout << ready.dump() << '\n' << std::flush;
      std::string line;
      while (std::getline(std::cin, line)) {
        if (line.empty()) {
          continue;
        }
        Json reply;
        try {
          const auto request_json = Json::parse(line);
          pb::QueryWorkerRequest request;
          request.entry_node = request_json.at("entryNode").get<std::uint32_t>();
          for (const auto &index : request_json.value("actionIndices", Json::array())) {
            request.action_indices.push_back(index.get<std::uint32_t>());
          }
          for (const auto &card : request_json.value("board", Json::array())) {
            const auto parsed = gtosd::parse_card(card.get<std::string>());
            if (!parsed) {
              throw std::runtime_error("invalid board card " + card.get<std::string>());
            }
            request.board.push_back(parsed.value());
          }
          request.samples_per_action = request_json.value("samplesPerAction", 64U);
          request.seed = request_json.value("seed", 20260913ULL);
          const auto response = worker.query(request);
          if (!response) {
            throw std::runtime_error(std::string("query failed: ") +
                                     pb::query_worker_error_name(response.error()));
          }
          std::vector<gtosd::CardId> visible(request.board.begin(),
                                             request.board.begin() + response.value().visible_board_cards);
          reply = Json::parse(pb::query_worker_response_json(response.value(), compiled.value(), visible));
          reply["samplesPerAction"] = request.samples_per_action;
          reply["iterations"] = options.iterations;
          reply["backend"] = "preflop_blueprint";
        } catch (const std::exception &error) {
          reply = Json{{"error", error.what()}};
        }
        std::cout << reply.dump() << '\n' << std::flush;
      }
      return 0;
    }

    if (!query_hand.empty()) {
      pb::QueryTables tables;
      tables.catalog = &catalog;
      tables.flop = &flop.value();
      tables.turn = &turn.value();
      tables.river = &river.value();
      pb::QueryRequest request;
      request.actions = split(query_history, ',');
      const auto hand = parse_cards(query_hand);
      if (hand.size() != 2U) {
        throw std::runtime_error("--query-hand needs two cards");
      }
      request.hand = {hand[0], hand[1]};
      request.board = parse_cards(query_board);
      const auto result = pb::query_policy(compiled.value(), policy, tables, request);
      if (!result) {
        throw std::runtime_error(std::string("query failed: ") + pb::query_error_name(result.error()));
      }
      Json json;
      json["schema"] = "gtosd.preflop_blueprint_query.v1";
      json["node"] = result.value().node;
      json["node_id"] = result.value().node_id;
      json["street"] = pb::street_name(result.value().street);
      json["actor"] = game_config.value().positions[result.value().actor];
      json["row"] = result.value().row;
      json["board_cards_used"] = result.value().board_cards_used;
      Json actions = Json::object();
      for (const auto &action : result.value().actions) {
        actions[action.id] = action.probability;
      }
      json["actions"] = actions;
      json["policy_fingerprint"] = pb::policy_fingerprint(policy);
      std::cout << json.dump(2) << "\nPREFLOP_BLUEPRINT_QUERY=OK\n";
      return 0;
    }

    auto ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
    auto all_in = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
    if (!ranks || !all_in) {
      throw std::runtime_error("resources missing");
    }
    pb::BestResponseResources resources;
    resources.ranks = &ranks.value();
    resources.all_in = &all_in.value();
    resources.catalog = &catalog;
    resources.flop = &flop.value();
    resources.turn = &turn.value();
    resources.river = &river.value();
    pb::Certificate certificate;
    if (!certificate_path.empty()) {
      certificate = load_certificate(certificate_path);
      options.certificate = &certificate;
    }
    const auto chart = pb::export_chart(compiled.value(), policy, resources, options);
    if (!chart) {
      throw std::runtime_error(std::string("export failed: ") +
                               pb::chart_export_error_name(chart.error()));
    }
    if (!output_path.empty()) {
      const auto written = pb::write_text_atomically(output_path, chart.value().json);
      if (!written) {
        throw std::runtime_error("cannot write " + output_path.string());
      }
    }
    const auto &result = chart.value();
    std::cout << "{\"event\": \"export\", \"config_id\": \"" << game_config.value().id
              << "\", \"badge\": \"" << result.badge << "\", \"nodes\": " << result.nodes
              << ", \"flops\": " << result.estimate.flops << ", \"boards\": " << result.estimate.boards
              << ", \"root_ev\": " << result.root_ev[0] << ", \"max_gain\": " << result.estimate.max_gain
              << ", \"max_gain_lower\": " << result.estimate.max_gain_lower
              << ", \"max_gain_half_width\": " << result.estimate.max_gain_half_width
              << ", \"policy_fingerprint\": \"" << result.policy_fingerprint
              << "\", \"checksum\": \"" << result.checksum << "\", \"bytes\": " << result.json.size()
              << ", \"seconds\": " << std::chrono::duration<double>(Clock::now() - started).count()
              << "}\n";
    std::cout << "PREFLOP_BLUEPRINT_EXPORT=" << result.badge << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_EXPORT=FAIL " << error.what() << '\n';
    return 1;
  }
}
