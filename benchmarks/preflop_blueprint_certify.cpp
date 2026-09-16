// Board-major certifier of the preflop blueprint (roadmap P7): exact best
// response of the physical game against a bucket policy over all canonical
// flops with their runouts, resumable by chunks; or the sampled estimator of
// P6 as a separate command (--sample-flops). Writes the certificate JSON.
#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/certifier.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;

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

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path config_path;
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path policy_path;
    std::filesystem::path output_path;
    bool uniform = false;
    pb::CertifierOptions options;
    options.threads = 1U;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--uniform") {
        uniform = true;
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
      } else if (name == "--output") {
        output_path = std::filesystem::path(value);
      } else if (name == "--state") {
        options.state_path = std::filesystem::path(value);
      } else if (name == "--threads") {
        options.threads = static_cast<unsigned>(parse_unsigned(value));
      } else if (name == "--chunk") {
        options.chunk_flops = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--flop-limit") {
        options.flop_limit = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--sample-flops") {
        options.sample_flops = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--sample-seed") {
        options.sample_seed = parse_unsigned(value);
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    if (config_path.empty() || resources_dir.empty() || buckets_dir.empty()) {
      throw std::runtime_error("--config, --resources-dir and --buckets-dir are required");
    }
    if (policy_path.empty() == !uniform) {
      throw std::runtime_error("exactly one of --policy and --uniform is required");
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
    auto ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
    auto all_in = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
    auto flop = ca::BucketTable::load(buckets_dir / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(buckets_dir / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(buckets_dir / "river_buckets_v1.bin");
    if (!ranks || !all_in || !flop || !turn || !river) {
      throw std::runtime_error("resources or bucket tables missing");
    }
    const auto catalog = ca::BoardCatalog::build();
    pb::BestResponseResources resources;
    resources.ranks = &ranks.value();
    resources.all_in = &all_in.value();
    resources.catalog = &catalog;
    resources.flop = &flop.value();
    resources.turn = &turn.value();
    resources.river = &river.value();

    std::string policy_source = "uniform";
    pb::BucketPolicy policy(compiled.value(),
                            pb::layout_state(compiled.value(), flop.value().capacity(),
                                             turn.value().capacity(), river.value().capacity()));
    if (!uniform) {
      pb::PolicyFileInfo info;
      auto loaded = pb::load_policy(policy_path, compiled.value(), &info);
      if (!loaded) {
        throw std::runtime_error(std::string("policy rejected: ") +
                                 pb::policy_file_error_name(loaded.error()));
      }
      policy = std::move(*loaded.value());
      policy_source = info.source;
    }

    std::cout << "{\"event\": \"start\", \"config_id\": \"" << game_config.value().id
              << "\", \"tree_fingerprint\": \"" << compiled.value().fingerprint()
              << "\", \"policy_fingerprint\": \"" << pb::policy_fingerprint(policy)
              << "\", \"policy_source\": \"" << policy_source
              << "\", \"canonical_flops\": " << catalog.flops().size()
              << ", \"threads\": " << options.threads << ", \"chunk\": " << options.chunk_flops
              << ", \"flop_limit\": " << options.flop_limit
              << ", \"sample_flops\": " << options.sample_flops << ", \"preparation_seconds\": "
              << std::chrono::duration<double>(Clock::now() - started).count() << "}\n";

    options.progress = [&](const pb::CertifierProgress &progress) {
      const double rate = progress.flops_done > 0U ? progress.seconds / progress.flops_done : 0.0;
      std::cout << "{\"event\": \"progress\", \"flops_done\": " << progress.flops_done
                << ", \"flops_total\": " << progress.flops_total
                << ", \"boards_done\": " << progress.boards_done
                << ", \"elapsed_seconds\": " << progress.seconds
                << ", \"seconds_per_flop\": " << rate << ", \"eta_seconds\": "
                << rate * static_cast<double>(progress.flops_total - progress.flops_done) << "}\n"
                << std::flush;
    };
    const auto certificate = pb::certify(compiled.value(), policy, resources, options);
    if (!certificate) {
      throw std::runtime_error(std::string("certification failed: ") +
                               pb::certifier_error_name(certificate.error()));
    }
    const auto json = pb::certificate_json(certificate.value());
    if (!output_path.empty()) {
      std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
      if (!output) {
        throw std::runtime_error("cannot write " + output_path.string());
      }
      output << json;
    }
    std::cout << json;
    const auto &result = certificate.value();
    std::cout << "PREFLOP_BLUEPRINT_CERTIFY="
              << (result.sampled ? "SAMPLED" : result.partial ? "PARTIAL" : "EXACT") << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CERTIFY=FAIL " << error.what() << '\n';
    return 1;
  }
}
