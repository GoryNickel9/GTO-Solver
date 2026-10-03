// Board-major certifier of the preflop blueprint (roadmap P7): exact best
// response of the physical game against a bucket policy over all canonical
// flops with their runouts, resumable by chunks; or the sampled estimator of
// P6 as a separate command (--sample-flops). Writes the certificate JSON.
// --deviation-from preflop|flop|turn|river|none restricts the best responder
// to the streets from the given one on (a diagnostic; preflop is the default
// full best response, none never deviates and gains exactly zero).
#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/certifier.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <memory>
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

double parse_decimal(const std::string_view text) {
  std::size_t consumed = 0U;
  const auto parsed = std::stod(std::string{text}, &consumed);
  if (consumed != text.size() || !std::isfinite(parsed))
    throw std::runtime_error("invalid decimal: " + std::string{text});
  return parsed;
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
    bool use_class_rows = false;
    std::filesystem::path reference_path;
    std::optional<pb::ClassBucketRows> class_rows;
    double target_pot_percent = 1.0;
    pb::CertifierOptions options;
    options.threads = 1U;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--class-rows") {
        use_class_rows = true;
        continue;
      }
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
      } else if (name == "--reference-certificate") {
        reference_path = std::filesystem::path(value);
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
      } else if (name == "--target-pot-percent") {
        target_pot_percent = parse_decimal(value);
      } else if (name == "--river-engine") {
        if (value == "joint") {
          options.river_engine = pb::RiverEngine::Joint;
        } else if (value == "reference") {
          options.river_engine = pb::RiverEngine::Reference;
        } else {
          throw std::runtime_error("--river-engine must be joint or reference");
        }
      } else if (name == "--deviation-from") {
        const auto street = pb::parse_deviation_street(value);
        if (!street) {
          throw std::runtime_error("--deviation-from must be preflop, flop, turn, river or none");
        }
        options.deviation_from = *street;
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
    if (!(target_pot_percent > 0.0 && target_pot_percent <= 100.0))
      throw std::runtime_error("--target-pot-percent must be in (0, 100]");

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
    if (use_class_rows) {
      if (uniform) {
        throw std::runtime_error(
            "--class-rows requires a saved policy with table identity or a reference certificate");
      }
      auto mapped = pb::ClassBucketRows::build(flop.value(), turn.value(), river.value());
      if (!mapped) {
        throw std::runtime_error("class row reconstruction failed");
      }
      class_rows.emplace(std::move(mapped.value()));
      resources.class_rows = &*class_rows;
    }

    std::string policy_source = "uniform";
    const auto flop_capacity = flop.value().capacity();
    const auto turn_capacity = turn.value().capacity();
    const auto river_capacity = river.value().capacity();
    // The policy is held through a pointer: a uniform placeholder table followed by a
    // move-assignment of the loaded table kept two full tables alive (7.6 GB on HU20).
    std::unique_ptr<pb::BucketPolicy> policy_holder;
    if (uniform) {
      policy_holder = std::make_unique<pb::BucketPolicy>(
          compiled.value(),
          pb::layout_state(compiled.value(), flop_capacity, turn_capacity, river_capacity));
    } else {
      pb::PolicyFileInfo info;
      auto loaded = pb::load_policy(policy_path, compiled.value(), &info);
      if (!loaded) {
        throw std::runtime_error(std::string("policy rejected: ") +
                                 pb::policy_file_error_name(loaded.error()));
      }
      policy_holder = std::move(loaded.value());
      policy_source = info.source;
    }
    const pb::BucketPolicy &policy = *policy_holder;

    if (use_class_rows) {
      const std::array<std::uint32_t, 3> counts{class_rows->count(ca::BucketStreet::Flop),
                                                class_rows->count(ca::BucketStreet::Turn),
                                                class_rows->count(ca::BucketStreet::River)};
      const std::string expected_source =
          "|abstraction=class-major-v1|flop=" + flop.value().fingerprint() +
          "|turn=" + turn.value().fingerprint() + "|river=" + river.value().fingerprint();
      if (reference_path.empty()) {
        const auto &layout = policy.layout();
        if (!policy_source.ends_with(expected_source) ||
            counts != std::array<std::uint32_t, 3>{layout.flop_capacity, layout.turn_capacity,
                                                   layout.river_capacity}) {
          throw std::runtime_error(
              "class policy lacks matching table identity; provide its reference certificate");
        }
      } else {
        const auto reference = nlohmann::json::parse(read_file(reference_path));
        if (reference.at("tree_fingerprint") != compiled.value().fingerprint() ||
            reference.at("policy_fingerprint") != pb::policy_fingerprint(policy) ||
            reference.at("flop_table_fingerprint") != flop.value().fingerprint() ||
            reference.at("turn_table_fingerprint") != turn.value().fingerprint() ||
            reference.at("river_table_fingerprint") != river.value().fingerprint() ||
            reference.at("capacities") != nlohmann::json(counts)) {
          throw std::runtime_error("class reference certificate fingerprint/capacity mismatch");
        }
      }
    }
    const double preparation_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    const auto load_peaks = pb::process_memory_peaks();
    std::cout << "{\"event\": \"start\", \"config_id\": \"" << game_config.value().id
              << "\", \"tree_fingerprint\": \"" << compiled.value().fingerprint()
              << "\", \"policy_fingerprint\": \"" << pb::policy_fingerprint(policy)
              << "\", \"policy_source\": \"" << policy_source
              << "\", \"canonical_flops\": " << catalog.flops().size()
              << ", \"threads\": " << options.threads << ", \"chunk\": " << options.chunk_flops
              << ", \"river_engine\": \"" << pb::river_engine_name(options.river_engine) << "\""
              << ", \"deviation_from\": \"" << pb::deviation_street_name(options.deviation_from)
              << "\""
              << ", \"flop_limit\": " << options.flop_limit
              << ", \"sample_flops\": " << options.sample_flops << ", \"preparation_seconds\": "
              << preparation_seconds << ", \"process_after_load\": {\"working_set_bytes\": "
              << load_peaks.working_set_bytes << ", \"peak_working_set_bytes\": "
              << load_peaks.peak_working_set_bytes << ", \"private_commit_bytes\": "
              << load_peaks.private_commit_bytes << ", \"peak_private_commit_bytes\": "
              << load_peaks.peak_private_commit_bytes << "}}\n";

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
    auto json = nlohmann::json::parse(pb::certificate_json(certificate.value()));
    const auto target_antes =
        0.01 * target_pot_percent * certificate.value().initial_pot_antes;
    json["target_pot_percent"] = target_pot_percent;
    json["target_antes"] = target_antes;
    // A street-restricted gain bounds the exploitability from below only, so a
    // restricted certificate never passes the target.
    json["passes_target"] = certificate.value().exact && !certificate.value().partial &&
                            options.deviation_from == pb::DeviationStreet::Preflop &&
                            certificate.value().report.max_gain <= target_antes;
    {
      const auto peaks = pb::process_memory_peaks();
      json["process_peaks"] = {{"working_set_bytes", peaks.working_set_bytes},
                               {"peak_working_set_bytes", peaks.peak_working_set_bytes},
                               {"private_commit_bytes", peaks.private_commit_bytes},
                               {"peak_private_commit_bytes", peaks.peak_private_commit_bytes},
                               {"page_faults", peaks.page_faults}};
      json["policy_table_bytes"] = policy.table().size() * sizeof(double);
      json["preparation_seconds"] = preparation_seconds;
    }
    if (!output_path.empty()) {
      std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
      if (!output) {
        throw std::runtime_error("cannot write " + output_path.string());
      }
      output << json.dump(2) << '\n';
    }
    std::cout << json.dump(2) << '\n';
    const auto &result = certificate.value();
    std::cout << "PREFLOP_BLUEPRINT_CERTIFY="
              << (result.sampled ? "SAMPLED" : result.partial ? "PARTIAL" : "EXACT");
    if (result.report.deviation_from != pb::DeviationStreet::Preflop) {
      std::cout << " deviation_from=" << pb::deviation_street_name(result.report.deviation_from);
    }
    std::cout << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CERTIFY=FAIL " << error.what() << '\n';
    return 1;
  }
}
