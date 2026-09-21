// Diagnostic best response constrained to the saved perfect-recall history
// abstraction. Sampled mode uses the same physical-flop law as the physical
// certifier; exact mode enumerates every canonical flop with orbit weight.
#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/abstract_best_response.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint64_t parse_unsigned(const std::string_view text) {
  return std::stoull(std::string(text));
}
} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path config_path;
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path history_path;
    std::filesystem::path policy_path;
    std::filesystem::path output_path;
    unsigned threads = 1U;
    std::uint32_t sample_flops = 0U;
    std::uint64_t sample_seed = 0x4345'5254'4946'5931ULL;
    for (int index = 1; index < argc; ++index) {
      if (index + 1 >= argc)
        throw std::runtime_error("missing value for " + std::string(argv[index]));
      const std::string_view name = argv[index];
      const std::string_view value = argv[++index];
      if (name == "--config")
        config_path = value;
      else if (name == "--resources-dir")
        resources_dir = value;
      else if (name == "--buckets-dir")
        buckets_dir = value;
      else if (name == "--history-rows")
        history_path = value;
      else if (name == "--policy")
        policy_path = value;
      else if (name == "--output")
        output_path = value;
      else if (name == "--threads")
        threads = static_cast<unsigned>(parse_unsigned(value));
      else if (name == "--sample-flops")
        sample_flops = static_cast<std::uint32_t>(parse_unsigned(value));
      else if (name == "--sample-seed")
        sample_seed = parse_unsigned(value);
      else
        throw std::runtime_error("unknown argument " + std::string(name));
    }
    if (config_path.empty() || resources_dir.empty() || buckets_dir.empty() ||
        history_path.empty() || policy_path.empty() || threads == 0U)
      throw std::runtime_error(
          "--config, --resources-dir, --buckets-dir, --history-rows and --policy are required");

    const auto started = Clock::now();
    const auto config = pb::parse_game_config_json(read_file(config_path));
    if (!config)
      throw std::runtime_error("configuration rejected");
    const auto game = pb::CompiledGame::compile(config.value());
    if (!game)
      throw std::runtime_error("compile failed");
    auto ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
    auto all_in = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
    auto flop = ca::BucketTable::load(buckets_dir / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(buckets_dir / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(buckets_dir / "river_buckets_v1.bin");
    auto history = pb::HistoryBucketRows::load(history_path);
    if (!ranks || !all_in || !flop || !turn || !river || !history)
      throw std::runtime_error("resource, bucket or history map load failed");
    const auto catalog = ca::BoardCatalog::build();
    pb::PolicyFileInfo policy_info;
    auto policy = pb::load_policy(policy_path, game.value(), &policy_info);
    if (!policy)
      throw std::runtime_error(std::string("policy rejected: ") +
                               pb::policy_file_error_name(policy.error()));
    const auto &layout = policy.value()->layout();
    if (!policy_info.source.ends_with("|abstraction=" +
                                     std::string(history.value().format_name()) + "|map=" +
                                     history.value().fingerprint()) ||
        layout.flop_capacity != history.value().count(ca::BucketStreet::Flop) ||
        layout.turn_capacity != history.value().count(ca::BucketStreet::Turn) ||
        layout.river_capacity != history.value().count(ca::BucketStreet::River))
      throw std::runtime_error("history policy and map identity mismatch");

    std::vector<pb::FlopGroup> groups;
    if (sample_flops > 0U) {
      ca::DeterministicRandom random(sample_seed);
      groups.reserve(sample_flops);
      for (std::uint32_t draw = 0; draw < sample_flops; ++draw)
        groups.push_back(pb::full_runouts(catalog.sample_physical_history(random).flop));
    } else {
      groups.reserve(catalog.flops().size());
      for (const auto &entry : catalog.flops())
        groups.push_back(pb::full_runouts(entry.cards, entry.multiplicity));
    }

    pb::TrainerResources resources;
    resources.ranks = &ranks.value();
    resources.all_in = &all_in.value();
    resources.catalog = &catalog;
    resources.flop = &flop.value();
    resources.turn = &turn.value();
    resources.river = &river.value();
    resources.history_rows = &history.value();
    pb::AbstractBestResponseOptions options;
    options.threads = threads;
    options.progress = [&](const pb::AbstractBestResponseProgress &progress) {
      const double per_flop =
          progress.flops_done > 0U ? progress.seconds / progress.flops_done : 0.0;
      std::cout << "{\"event\":\"progress\",\"flops_done\":" << progress.flops_done
                << ",\"flops_total\":" << progress.flops_total
                << ",\"boards_done\":" << progress.boards_done
                << ",\"elapsed_seconds\":" << std::setprecision(17) << progress.seconds
                << ",\"eta_seconds\":"
                << per_flop * static_cast<double>(progress.flops_total - progress.flops_done)
                << "}\n"
                << std::flush;
    };
    const auto fingerprint = policy_info.policy_fingerprint;
    auto report = pb::evaluate_abstract_best_response(
        game.value(), std::move(*policy.value()), resources, groups, options);
    if (!report)
      throw std::runtime_error(std::string("evaluation failed: ") +
                               pb::trainer_error_name(report.error()));

    Json output{{"schema", "gtosd.research.abstract_best_response.v1"},
                {"config_id", config.value().id},
                {"exact", sample_flops == 0U},
                {"sampled", sample_flops > 0U},
                {"sample_seed", sample_seed},
                {"flops", report.value().flops},
                {"boards", report.value().boards},
                {"gain", report.value().gain},
                {"max_gain", report.value().max_gain},
                {"tree_fingerprint", game.value().fingerprint()},
                {"policy_fingerprint", fingerprint},
                {"history_map_fingerprint", history.value().fingerprint()},
                {"capacities", {layout.flop_capacity, layout.turn_capacity, layout.river_capacity}},
                {"process_bytes", report.value().process_bytes},
                {"evaluation_seconds", report.value().seconds},
                {"total_seconds", std::chrono::duration<double>(Clock::now() - started).count()}};
    if (!output_path.empty()) {
      std::ofstream file(output_path, std::ios::binary | std::ios::trunc);
      file << output.dump(2) << '\n';
      if (!file)
        throw std::runtime_error("cannot write " + output_path.string());
    }
    std::cout << output.dump(2) << "\nPREFLOP_BLUEPRINT_ABSTRACT_BR=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_ABSTRACT_BR=FAIL " << error.what() << '\n';
    return 1;
  }
}
