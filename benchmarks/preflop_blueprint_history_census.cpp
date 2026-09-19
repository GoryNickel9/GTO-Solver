// Count exact history keys before allocating a perfect-recall candidate.
#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace pb = gtosd::preflop_blueprint;
namespace ca = gtosd::card_abstraction;
using Json = nlohmann::json;

int main(int argc, char **argv) {
  try {
    if (argc != 4 && argc != 5)
      throw std::runtime_error("usage: RESOURCES BUCKETS OUTPUT [--complete]");
    const bool complete_census = argc == 5 && std::string(argv[4]) == "--complete";
    if (argc == 5 && !complete_census)
      throw std::runtime_error("unknown option");
    const auto start = std::chrono::steady_clock::now();
    const std::filesystem::path resources(argv[1]), buckets(argv[2]);
    auto ranks = ca::RankTable::load(resources / "rank_table_v1.bin");
    auto flop = ca::BucketTable::load(buckets / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(buckets / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(buckets / "river_buckets_v1.bin");
    if (!ranks || !flop || !turn || !river)
      throw std::runtime_error("resources missing");
    const auto catalog = ca::BoardCatalog::build();
    auto class_rows = pb::ClassBucketRows::build(flop.value(), turn.value(), river.value());
    if (!class_rows)
      throw std::runtime_error("class mapping failed");
    pb::AbstractionTables tables{&catalog, &flop.value(), &turn.value(), &river.value()};
    std::vector<pb::CompiledGame> games;
    for (const auto name : {"hu20", "hu30", "co40"}) {
      std::ifstream input(std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks/fixtures" /
                          (std::string("preflop_blueprint_") + name + "_test_v1.json"));
      const std::string text{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
      const auto config = pb::parse_game_config_json(text);
      if (!config)
        throw std::runtime_error("config failed");
      auto game = pb::CompiledGame::compile(config.value());
      if (!game)
        throw std::runtime_error("compile failed");
      games.push_back(std::move(game.value()));
    }
    std::unordered_set<std::uint64_t> turn_keys, river_keys;
    constexpr std::uint64_t numeric_limit = 12ULL * 1024 * 1024 * 1024;
    Json output{{"schema", "gtosd.research.history_census.v1"},
                {"flop_table_fingerprint", flop.value().fingerprint()},
                {"turn_table_fingerprint", turn.value().fingerprint()},
                {"river_table_fingerprint", river.value().fingerprint()},
                {"numeric_limit_bytes", numeric_limit}};
    std::uint32_t completed = 0;
    for (const auto &entry : catalog.flops()) {
      const auto runouts = pb::full_runouts(entry.cards);
      for (const auto &board : runouts.boards) {
        auto context = pb::BoardContext::build(board.history, ranks.value(), &tables);
        if (!context)
          throw std::runtime_error("context failed");
        for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
          const auto fkey = static_cast<std::uint64_t>(context.value().hand_classes()[hand]) *
                                flop.value().capacity() +
                            context.value().row(gtosd::Street::Flop, hand);
          const auto tkey =
              fkey * turn.value().capacity() + context.value().row(gtosd::Street::Turn, hand);
          const auto rkey =
              tkey * river.value().capacity() + context.value().row(gtosd::Street::River, hand);
          turn_keys.insert(tkey);
          river_keys.insert(rkey);
        }
      }
      ++completed;
      output["flops_scanned"] = completed;
      output["complete"] = completed == catalog.flops().size();
      output["flop_rows_exact"] = class_rows.value().count(ca::BucketStreet::Flop);
      output["turn_rows_lower_bound"] = turn_keys.size();
      output["river_rows_lower_bound"] = river_keys.size();
      output["games"] = Json::array();
      bool all_exceed = true;
      for (const auto &game : games) {
        const auto layout = pb::layout_state(game, class_rows.value().count(ca::BucketStreet::Flop),
                                             static_cast<std::uint32_t>(turn_keys.size()),
                                             static_cast<std::uint32_t>(river_keys.size()));
        const auto two = layout.entries * 16ULL;
        all_exceed = all_exceed && two > numeric_limit;
        output["games"].push_back({{"id", game.config().id},
                                   {"tree_fingerprint", game.fingerprint()},
                                   {"entries_lower_bound", layout.entries},
                                   {"regret_and_sum_bytes_lower_bound", two},
                                   {"including_policy_bytes_lower_bound", layout.entries * 24ULL}});
      }
      output["seconds"] =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::ofstream file(argv[3]);
      file << output.dump(2) << '\n';
      file.close();
      if (!file)
        throw std::runtime_error("report write failed");
      std::cout << "flops=" << completed << " turn=" << turn_keys.size()
                << " river=" << river_keys.size() << " all_exceed=" << all_exceed << '\n'
                << std::flush;
      if (all_exceed && !complete_census)
        break;
    }
    if (completed == catalog.flops().size()) {
      std::unordered_map<std::uint64_t, std::uint32_t> children;
      for (const auto key : river_keys)
        ++children[key / river.value().capacity()];
      output["river_children_per_exact_turn_histogram"] = Json::object();
      std::map<std::uint32_t, std::uint32_t> histogram;
      for (const auto &[parent, count] : children) {
        (void)parent;
        ++histogram[count];
      }
      for (const auto &[count, parents] : histogram)
        output["river_children_per_exact_turn_histogram"][std::to_string(count)] = parents;
      output["river_only_caps"] = Json::array();
      for (std::uint32_t cap = 1; cap <= 32; ++cap) {
        std::uint32_t rows = 0;
        for (const auto &[count, parents] : histogram)
          rows += std::min(cap, count) * parents;
        Json candidate{{"maximum_children", cap}, {"river_rows", rows}, {"games", Json::array()}};
        for (const auto &game : games) {
          const auto layout =
              pb::layout_state(game, class_rows.value().count(ca::BucketStreet::Flop),
                               static_cast<std::uint32_t>(turn_keys.size()), rows);
          candidate["games"].push_back(
              {{"id", game.config().id}, {"numeric_bytes", layout.entries * 24ULL}});
        }
        output["river_only_caps"].push_back(std::move(candidate));
      }
      std::ofstream file(argv[3]);
      file << output.dump(2) << '\n';
      file.close();
      if (!file)
        throw std::runtime_error("final report write failed");
    }
    std::cout << "HISTORY_CENSUS=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HISTORY_CENSUS=FAIL " << error.what() << '\n';
    return 1;
  }
}
