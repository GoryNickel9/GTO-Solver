#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace pb = gtosd::preflop_blueprint;
namespace ca = gtosd::card_abstraction;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

int main(int argc, char **argv) {
  try {
    if (argc != 6)
      throw std::runtime_error("usage: RESOURCES BUCKETS MAP REPORT MAXIMUM_CHILDREN");
    const auto started = Clock::now();
    const std::filesystem::path resources(argv[1]), buckets(argv[2]);
    const auto cap = static_cast<std::uint32_t>(std::stoul(argv[5]));
    auto ranks = ca::RankTable::load(resources / "rank_table_v1.bin");
    auto flop = ca::BucketTable::load(buckets / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(buckets / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(buckets / "river_buckets_v1.bin");
    if (!ranks || !flop || !turn || !river)
      throw std::runtime_error("resource load failed");
    const auto catalog = ca::BoardCatalog::build();
    pb::AbstractionTables tables{&catalog, &flop.value(), &turn.value(), &river.value()};
    std::unordered_map<std::uint64_t, std::uint64_t> weights;
    std::uint32_t completed = 0;
    for (const auto &entry : catalog.flops()) {
      for (const auto &board : pb::full_runouts(entry.cards).boards) {
        const auto context = pb::BoardContext::build(board.history, ranks.value(), &tables);
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
          weights[rkey] += entry.multiplicity;
        }
      }
      ++completed;
      if (completed % 50 == 0)
        std::cout << "flops=" << completed << " support=" << weights.size() << '\n' << std::flush;
    }
    const auto census_end = Clock::now();
    std::vector<pb::HistoryObservation> observations;
    observations.reserve(weights.size());
    for (const auto &[key, weight] : weights)
      observations.push_back({key, weight});
    weights.clear();
    weights.rehash(0);
    pb::HistoryClusteringReport clustering;
    auto rows = pb::HistoryBucketRows::build(flop.value(), turn.value(), river.value(),
                                             std::move(observations), cap, &clustering);
    if (!rows)
      throw std::runtime_error("history clustering failed");
    const auto clustering_end = Clock::now();
    if (!rows.value().save(argv[3]))
      throw std::runtime_error("map save failed (output must be new)");
    const auto loaded = pb::HistoryBucketRows::load(argv[3]);
    if (!loaded || loaded.value().fingerprint() != rows.value().fingerprint())
      throw std::runtime_error("map roundtrip failed");
    const std::uint64_t expected_weight =
        static_cast<std::uint64_t>(ca::physical_board_histories) * pb::live_hand_count;
    if (clustering.weight != expected_weight)
      throw std::runtime_error("physical support weight mismatch");
    Json output{
        {"schema", "gtosd.research.history_rows.v1"},
        {"complete", true},
        {"maximum_children", cap},
        {"fingerprint", rows.value().fingerprint()},
        {"base_tables",
         {flop.value().fingerprint(), turn.value().fingerprint(), river.value().fingerprint()}},
        {"capacities",
         {rows.value().count(ca::BucketStreet::Flop), rows.value().count(ca::BucketStreet::Turn),
          rows.value().count(ca::BucketStreet::River)}},
        {"support", clustering.support},
        {"physical_weight", clustering.weight},
        {"weighted_squared_centroid_distance", clustering.weighted_squared_distance},
        {"maximum_squared_centroid_distance", clustering.maximum_squared_distance},
        {"maximum_lloyd_iterations", clustering.maximum_iterations},
        {"map_bytes", rows.value().byte_size()},
        {"census_seconds", std::chrono::duration<double>(census_end - started).count()},
        {"clustering_seconds", std::chrono::duration<double>(clustering_end - census_end).count()},
        {"total_seconds", std::chrono::duration<double>(Clock::now() - started).count()}};
    output["games"] = Json::array();
    for (const auto name : {"hu20", "hu30", "co40"}) {
      std::ifstream input(std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks/fixtures" /
                          (std::string("preflop_blueprint_") + name + "_test_v1.json"));
      const std::string text{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
      const auto config = pb::parse_game_config_json(text);
      if (!config)
        throw std::runtime_error("config failed");
      const auto game = pb::CompiledGame::compile(config.value());
      if (!game)
        throw std::runtime_error("game failed");
      const auto layout = pb::layout_state(game.value(), rows.value().count(ca::BucketStreet::Flop),
                                           rows.value().count(ca::BucketStreet::Turn),
                                           rows.value().count(ca::BucketStreet::River));
      output["games"].push_back({{"id", config.value().id},
                                 {"tree_fingerprint", game.value().fingerprint()},
                                 {"entries", layout.entries},
                                 {"numeric_bytes", layout.entries * 24ULL}});
    }
    std::ofstream report(argv[4]);
    report << output.dump(2) << '\n';
    report.close();
    if (!report)
      throw std::runtime_error("report write failed");
    std::cout << output.dump(2) << "\nHISTORY_ROWS=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HISTORY_ROWS=FAIL " << error.what() << '\n';
    return 1;
  }
}
