// Trains the preflop blueprint with vector CFR and public chance sampling and
// prints one JSON line per evaluation. Stops at the D3 rule (max gain plus
// half-width within one per cent of the initial pot) or at --iterations.

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"
#include "gtosd/preflop_blueprint/policy_file.hpp"

#include <algorithm>
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

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;

std::uint64_t parse_unsigned(const std::string_view value) {
  std::size_t consumed = 0U;
  const auto parsed = std::stoull(std::string{value}, &consumed, 10);
  if (consumed != value.size()) {
    throw std::runtime_error("invalid number: " + std::string{value});
  }
  return parsed;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void print_estimate(const std::uint64_t iteration, const double elapsed,
                    const pb::ExploitabilityEstimate &estimate, const std::uint64_t process_bytes,
                    const std::uint64_t boards_processed) {
  std::cout << "{\"event\": \"evaluation\", \"iteration\": " << iteration
            << ", \"elapsed_seconds\": " << elapsed << ", \"boards_processed\": " << boards_processed
            << ", \"evaluation_flops\": " << estimate.flops << ", \"evaluation_boards\": " << estimate.boards
            << ", \"exact\": " << (estimate.exact ? "true" : "false")
            << ", \"ev\": [" << estimate.ev[0] << ", " << estimate.ev[1] << "]"
            << ", \"best_response\": [" << estimate.best_response[0] << ", "
            << estimate.best_response[1] << "]"
            << ", \"gain\": [" << estimate.gain[0] << ", " << estimate.gain[1] << "]"
            << ", \"gain_lower\": [" << estimate.gain_lower[0] << ", " << estimate.gain_lower[1] << "]"
            << ", \"gain_standard_error\": [" << estimate.gain_standard_error[0] << ", "
            << estimate.gain_standard_error[1] << "]"
            << ", \"max_gain\": " << estimate.max_gain
            << ", \"max_gain_lower\": " << estimate.max_gain_lower
            << ", \"max_gain_half_width\": " << estimate.max_gain_half_width
            << ", \"nashconv\": " << estimate.nashconv
            << ", \"normalized_dev\": " << estimate.normalized_dev
            << ", \"normalized_stack\": " << estimate.normalized_stack
            << ", \"evaluation_seconds\": " << estimate.seconds
            << ", \"process_bytes\": " << process_bytes << "}\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path config_path;
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path checkpoint_path;
    // Average policy written at the end (for the certifier and the export).
    std::filesystem::path policy_path;
    bool resume = false;
    std::uint64_t iterations = 100U;
    std::uint32_t evaluation_flops = 20U;
    std::uint64_t evaluate_every = 10U;
    // Diagnostic exact mode: the first N boards drawn with the training seed
    // form a fixed, equally weighted board list processed in full every
    // iteration and evaluated exactly.
    std::uint32_t fixed_boards = 0U;
    bool permute_suits = false;
    // Evaluate the (resumed) state once with --eval-flops and exit: used to
    // compare checkpoints on the same evaluation flops (same --eval-seed).
    bool evaluate_only = false;
    // --eval-seed: applied after a checkpoint load (the identity of a resumed
    // run keeps the configured seed), so a restored state can be evaluated on
    // fresh flops.
    std::optional<std::uint64_t> evaluation_seed;
    pb::TrainerConfig config;
    // Blueprint defaults (P6 report section 5): DCFR with alternating updates
    // converges two to three times faster than Linear simultaneous on HU10.
    config.scheme = pb::WeightingScheme::Dcfr;
    config.update_mode = pb::UpdateMode::Alternating;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--resume") {
        resume = true;
        continue;
      }
      if (name == "--permute-suits") {
        permute_suits = true;
        continue;
      }
      if (name == "--eval-only") {
        evaluate_only = true;
        continue;
      }
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{name});
      }
      const std::string_view value = argv[++index];
      if (name == "--config") {
        config_path = value;
      } else if (name == "--resources-dir") {
        resources_dir = value;
      } else if (name == "--buckets-dir") {
        buckets_dir = value;
      } else if (name == "--policy-out") {
        policy_path = std::filesystem::path(value);
      } else if (name == "--checkpoint") {
        checkpoint_path = value;
      } else if (name == "--iterations") {
        iterations = parse_unsigned(value);
      } else if (name == "--batch") {
        config.batch_boards = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--threads") {
        config.threads = static_cast<unsigned>(parse_unsigned(value));
      } else if (name == "--eval-flops") {
        evaluation_flops = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--eval-every") {
        evaluate_every = parse_unsigned(value);
      } else if (name == "--scheme") {
        if (value == "linear") {
          config.scheme = pb::WeightingScheme::Linear;
        } else if (value == "dcfr") {
          config.scheme = pb::WeightingScheme::Dcfr;
        } else {
          throw std::runtime_error("unknown scheme " + std::string{value});
        }
      } else if (name == "--update") {
        if (value == "simultaneous") {
          config.update_mode = pb::UpdateMode::Simultaneous;
        } else if (value == "alternating") {
          config.update_mode = pb::UpdateMode::Alternating;
        } else {
          throw std::runtime_error("unknown update mode " + std::string{value});
        }
      } else if (name == "--seed") {
        config.training_seed = parse_unsigned(value);
      } else if (name == "--eval-seed") {
        evaluation_seed = parse_unsigned(value);
      } else if (name == "--partition-target") {
        config.partition_target_nodes = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--fixed-boards") {
        fixed_boards = static_cast<std::uint32_t>(parse_unsigned(value));
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    if (config_path.empty() || resources_dir.empty() || buckets_dir.empty()) {
      throw std::runtime_error("--config, --resources-dir and --buckets-dir are required");
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
    config.flop_capacity = flop.value().capacity();
    config.turn_capacity = turn.value().capacity();
    config.river_capacity = river.value().capacity();
    pb::TrainerResources resources;
    resources.ranks = &ranks.value();
    resources.all_in = &all_in.value();
    resources.catalog = &catalog;
    resources.flop = &flop.value();
    resources.turn = &turn.value();
    resources.river = &river.value();
    std::optional<pb::TrainingBoards> boards;
    if (fixed_boards > 0U) {
      boards.emplace();
      ca::DeterministicRandom random(config.training_seed);
      for (std::uint32_t board = 0; board < fixed_boards; ++board) {
        const auto history = catalog.sample_physical_history(random);
        boards->histories.push_back(history);
        boards->weights.push_back(1.0);
        if (permute_suits) {
          // Diagnostic: a suit-rotated copy is the same canonical board, so
          // the pair must behave exactly like the single board.
          const ca::SuitPermutation rotation{1U, 2U, 3U, 0U};
          ca::BoardHistory rotated;
          for (std::size_t index = 0; index < 3U; ++index) {
            rotated.flop[index] = ca::permute_card(history.flop[index], rotation);
          }
          std::sort(rotated.flop.begin(), rotated.flop.end());
          rotated.turn = ca::permute_card(history.turn, rotation);
          rotated.river = ca::permute_card(history.river, rotation);
          boards->histories.push_back(rotated);
          boards->weights.push_back(1.0);
        }
      }
      boards->sample = false;
    }
    auto created = pb::Trainer::create(compiled.value(), resources, config,
                                       boards ? &boards.value() : nullptr);
    if (!created) {
      throw std::runtime_error(std::string("trainer creation failed: ") +
                               pb::trainer_error_name(created.error()));
    }
    auto &trainer = *created.value();
    if (resume && !checkpoint_path.empty() && std::filesystem::exists(checkpoint_path)) {
      const auto loaded = trainer.load_checkpoint(checkpoint_path);
      if (!loaded) {
        throw std::runtime_error(std::string("checkpoint rejected: ") +
                                 pb::trainer_error_name(loaded.error()));
      }
    }
    if (evaluation_seed.has_value()) {
      trainer.reseed_evaluation(*evaluation_seed);
    }
    const auto &stats = compiled.value().stats();
    std::cout << "{\"event\": \"start\", \"config_id\": \"" << game_config.value().id
              << "\", \"tree_fingerprint\": \"" << compiled.value().fingerprint()
              << "\", \"trainer_identity\": \"" << trainer.identity()
              << "\", \"nodes\": " << stats.node_count << ", \"decisions\": " << stats.decision_nodes
              << ", \"capacities\": [" << config.flop_capacity << ", " << config.turn_capacity
              << ", " << config.river_capacity << "], \"state_bytes\": " << trainer.state_bytes()
              << ", \"units\": " << trainer.partition().unit_roots.size()
              << ", \"top_nodes\": " << trainer.partition().top_nodes
              << ", \"largest_unit\": " << trainer.partition().largest_unit_nodes
              << ", \"batch\": " << config.batch_boards << ", \"threads\": " << config.threads
              << ", \"scheme\": \"" << pb::weighting_scheme_name(config.scheme)
              << "\", \"update\": \"" << pb::update_mode_name(config.update_mode)
              << "\", \"resumed_iteration\": " << trainer.iteration()
              << ", \"initial_pot_antes\": " << trainer.initial_pot_antes()
              << ", \"preparation_seconds\": "
              << std::chrono::duration<double>(Clock::now() - started).count() << "}\n";

    double training_seconds = 0.0;
    double evaluation_seconds = 0.0;
    bool converged = false;
    std::uint64_t process_bytes = 0U;
    if (evaluate_only) {
      const auto estimate = trainer.estimate_exploitability(evaluation_flops);
      if (!estimate) {
        throw std::runtime_error(std::string("evaluation failed: ") +
                                 pb::trainer_error_name(estimate.error()));
      }
      evaluation_seconds += estimate.value().seconds;
      print_estimate(trainer.iteration(),
                     std::chrono::duration<double>(Clock::now() - started).count(),
                     estimate.value(), pb::process_working_set_bytes(), trainer.boards_processed());
      converged = estimate.value().meets_stop_rule(trainer.initial_pot_antes());
      iterations = trainer.iteration();
    }
    while (trainer.iteration() < iterations && !converged) {
      const auto telemetry = trainer.iterate();
      if (!telemetry) {
        throw std::runtime_error(std::string("iteration failed: ") +
                                 pb::trainer_error_name(telemetry.error()));
      }
      training_seconds += telemetry.value().seconds;
      process_bytes = telemetry.value().process_bytes;
      const bool evaluate = evaluate_every > 0U && (trainer.iteration() % evaluate_every == 0U ||
                                                    trainer.iteration() == iterations);
      if (!evaluate) {
        continue;
      }
      const auto estimate = trainer.estimate_exploitability(evaluation_flops);
      if (!estimate) {
        throw std::runtime_error(std::string("evaluation failed: ") +
                                 pb::trainer_error_name(estimate.error()));
      }
      evaluation_seconds += estimate.value().seconds;
      print_estimate(trainer.iteration(),
                     std::chrono::duration<double>(Clock::now() - started).count(),
                     estimate.value(), process_bytes, trainer.boards_processed());
      converged = estimate.value().meets_stop_rule(trainer.initial_pot_antes());
      if (!checkpoint_path.empty()) {
        const auto saved = trainer.save_checkpoint(checkpoint_path);
        if (!saved) {
          throw std::runtime_error("checkpoint write failed");
        }
      }
    }
    std::string policy_fingerprint_text;
    if (!policy_path.empty()) {
      const auto average = trainer.average_policy();
      const auto saved = pb::save_policy(policy_path, compiled.value(), average,
                                         trainer.identity() + "|iteration=" +
                                             std::to_string(trainer.iteration()));
      if (!saved) {
        throw std::runtime_error(std::string("policy write failed: ") +
                                 pb::policy_file_error_name(saved.error()));
      }
      policy_fingerprint_text = pb::policy_fingerprint(average);
    }
    std::cout << "{\"event\": \"end\", \"iteration\": " << trainer.iteration()
              << ", \"policy_fingerprint\": \"" << policy_fingerprint_text << "\""
              << ", \"boards_processed\": " << trainer.boards_processed()
              << ", \"converged\": " << (converged ? "true" : "false")
              << ", \"training_seconds\": " << training_seconds
              << ", \"evaluation_seconds\": " << evaluation_seconds
              << ", \"seconds_per_iteration\": "
              << (trainer.iteration() > 0U ? training_seconds / static_cast<double>(trainer.iteration()) : 0.0)
              << ", \"state_fingerprint\": \"" << trainer.state_fingerprint()
              << "\", \"process_bytes\": " << pb::process_working_set_bytes()
              << ", \"total_seconds\": "
              << std::chrono::duration<double>(Clock::now() - started).count() << "}\n";
    std::cout << "PREFLOP_BLUEPRINT_TRAIN=" << (converged ? "CONVERGED" : "ITERATION_LIMIT") << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAIN=FAIL " << error.what() << '\n';
    return 1;
  }
}
