// Trains the preflop blueprint with vector CFR and public chance sampling and
// prints one JSON line per evaluation. In automatic mode the only convergence
// input is --target-pot-percent (default 1): internal checkpoints double from
// 250 iterations, a full physical best response validates a candidate, and
// a stalled curve exits as PLATEAU. --iterations remains a research override.

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/board_texture.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/certifier.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"
#include "monker_chart_format.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

double parse_decimal(const std::string_view value) {
  std::size_t consumed = 0U;
  const auto parsed = std::stod(std::string{value}, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed))
    throw std::runtime_error("invalid decimal: " + std::string{value});
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
                    const std::uint64_t boards_processed,
                    const std::string_view role = "scheduled") {
  std::cout << "{\"event\": \"evaluation\", \"role\": \"" << role
            << "\", \"iteration\": " << iteration
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

std::string array_text(const std::array<std::uint64_t, 4> &values) {
  std::string text = "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    text += (index == 0U ? "" : ", ");
    text += std::to_string(values[index]);
  }
  return text + "]";
}

nlohmann::json memory_breakdown_json(const pb::MemoryBreakdown &breakdown) {
  return nlohmann::json{
      {"event", "memory_breakdown"},
      {"regret_bytes", breakdown.regret_bytes},
      {"strategy_sum_bytes", breakdown.strategy_sum_bytes},
      {"regret_bytes_per_cell", breakdown.regret_bytes_per_cell},
      {"strategy_sum_bytes_per_cell", breakdown.strategy_sum_bytes_per_cell},
      {"compact_policy_capacity_bytes", breakdown.compact_policy_capacity_bytes},
      {"compact_policy_offsets_bytes", breakdown.compact_policy_offsets_bytes},
      {"discount_timestamp_bytes", breakdown.discount_timestamp_bytes},
      {"discount_factor_bytes", breakdown.discount_factor_bytes},
      {"discount_offset_bytes", breakdown.discount_offset_bytes},
      {"all_in_dense_bytes", breakdown.all_in_dense_bytes},
      {"board_batch_bytes", breakdown.board_batch_bytes},
      {"workspace_bytes", breakdown.workspace_bytes},
      {"unit_bytes", breakdown.unit_bytes},
      {"layout_offset_bytes", breakdown.layout_offset_bytes},
      {"partition_bytes", breakdown.partition_bytes},
      {"board_list_bytes", breakdown.board_list_bytes},
      {"hand_mask_bytes", breakdown.hand_mask_bytes},
      {"tree_bytes", breakdown.tree_bytes},
      {"history_map_resident_bytes", breakdown.history_map_resident_bytes},
      {"bucket_table_bytes", breakdown.bucket_table_bytes},
      {"rank_table_bytes", breakdown.rank_table_bytes},
      {"catalog_bytes", breakdown.catalog_bytes},
      {"all_in_table_bytes", breakdown.all_in_table_bytes},
      {"cells_by_street", breakdown.cells_by_street},
      {"rows_by_street", breakdown.rows_by_street},
      {"accounted_total_bytes", breakdown.total()}};
}

nlohmann::json peaks_json(const pb::ProcessMemoryPeaks &peaks) {
  return nlohmann::json{{"working_set_bytes", peaks.working_set_bytes},
                        {"peak_working_set_bytes", peaks.peak_working_set_bytes},
                        {"private_commit_bytes", peaks.private_commit_bytes},
                        {"peak_private_commit_bytes", peaks.peak_private_commit_bytes},
                        {"page_faults", peaks.page_faults}};
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
    std::filesystem::path current_policy_path;
    std::filesystem::path certificate_path;
    std::filesystem::path coverage_path;
    bool use_class_rows = false;
    // MonkerSolver-style rows: (board class, per-board bucket); the bucket
    // tables must be per-board tables (preflop_blueprint_monker_buckets).
    bool use_board_class_rows = false;
    // Texture of the board classes (board_texture.hpp): merged turn classes
    // sharing their rows. Without it every canonical board is its own class.
    std::filesystem::path board_texture_path;
    std::filesystem::path history_rows_path;
    bool resume = false;
    std::uint64_t iterations = 100U;
    bool automatic_target = true;
    bool batch_explicit = false;
    bool threads_explicit = false;
    double target_pot_percent = 1.0;
    // Automatic checkpoints are a paired trend screen, not a certificate.
    // Keep the sample deliberately small; a candidate is always validated by
    // the full physical best response below before it can pass.
    std::uint32_t evaluation_flops = 8U;
    std::uint64_t evaluate_every = 10U;
    std::uint64_t progress_every = 0U;
    std::uint64_t checkpoint_every = 0U;
    // MonkerSolver-format preflop charts every N iterations under chart_dir/it_<N>
    // (written to a .tmp directory and renamed when complete), read from the live
    // tables without stopping or perturbing the training.
    std::uint64_t chart_every = 0U;
    std::filesystem::path chart_dir;
    // --policy-snapshots: every chart snapshot also holds the average policy of that
    // iteration (chart_dir/it_<N>/policy.bin, the --policy-out format and source, the
    // bytes save_average_policy would write), read without modifying the state.
    // --policy-snapshot-every N (a multiple of --chart-every) keeps a policy only in
    // the snapshots whose iteration is a multiple of N. A snapshot policy is skipped
    // (policy_snapshot_skipped event) unless the chart directory's disk has room for
    // it, for what the rest of the run still has to write (the temporary copy of the
    // next checkpoint, the final --policy-out) and for --policy-snapshot-reserve-gb
    // (default 1) more.
    bool policy_snapshots = false;
    std::uint64_t policy_snapshot_every = 0U;
    double policy_snapshot_reserve_gb = 1.0;
    // When this file appears the training stops after the current iteration and
    // saves as at the normal end (used by the step-2 stability watcher).
    std::filesystem::path stop_file;
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
      if (name == "--batch-policy-refresh") {
        // Compatibility with the reference protocol: always in effect.
        config.batch_policy_refresh = true;
        continue;
      }
      if (name == "--lazy-discount") {
        config.lazy_discount = true;
        continue;
      }
      if (name == "--profile-traversal") {
        config.detailed_profile = true;
        continue;
      }
      if (name == "--reuse-discount-invariant-policy") {
        throw std::runtime_error("--reuse-discount-invariant-policy is no longer supported: the "
                                 "compact policy is rebuilt for every batch");
      }
      if (name == "--class-rows") {
        use_class_rows = true;
        continue;
      }
      if (name == "--board-class-rows") {
        use_board_class_rows = true;
        continue;
      }
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
      if (name == "--policy-snapshots") {
        policy_snapshots = true;
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
      } else if (name == "--history-rows") {
        history_rows_path = value;
      } else if (name == "--board-texture-map") {
        if (value.empty())
          throw std::runtime_error("--board-texture-map needs a file");
        board_texture_path = value;
      } else if (name == "--policy-out") {
        policy_path = std::filesystem::path(value);
      } else if (name == "--current-policy-out") {
        current_policy_path = std::filesystem::path(value);
      } else if (name == "--certificate-out") {
        certificate_path = std::filesystem::path(value);
      } else if (name == "--coverage-out") {
        coverage_path = std::filesystem::path(value);
      } else if (name == "--checkpoint") {
        checkpoint_path = value;
      } else if (name == "--prefetch-refresh") {
        config.prefetch_refresh_rows = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--prefetch-update") {
        config.prefetch_update_hands = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--lazy-discount-epoch") {
        const auto epoch = parse_unsigned(value);
        if (epoch == 0U || epoch > 65'535U)
          throw std::runtime_error("--lazy-discount-epoch must be in 1..65535");
        config.lazy_discount_epoch = static_cast<std::uint32_t>(epoch);
      } else if (name == "--iterations") {
        iterations = parse_unsigned(value);
        automatic_target = false;
      } else if (name == "--target-pot-percent") {
        target_pot_percent = parse_decimal(value);
      } else if (name == "--batch") {
        config.batch_boards = static_cast<std::uint32_t>(parse_unsigned(value));
        batch_explicit = true;
      } else if (name == "--threads") {
        config.threads = static_cast<unsigned>(parse_unsigned(value));
        threads_explicit = true;
      } else if (name == "--eval-flops") {
        evaluation_flops = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--eval-every") {
        evaluate_every = parse_unsigned(value);
      } else if (name == "--progress-every") {
        progress_every = parse_unsigned(value);
      } else if (name == "--checkpoint-every") {
        checkpoint_every = parse_unsigned(value);
      } else if (name == "--chart-every") {
        chart_every = parse_unsigned(value);
      } else if (name == "--chart-dir") {
        chart_dir = std::filesystem::path(value);
      } else if (name == "--policy-snapshot-every") {
        policy_snapshot_every = parse_unsigned(value);
      } else if (name == "--policy-snapshot-reserve-gb") {
        policy_snapshot_reserve_gb = parse_decimal(value);
      } else if (name == "--stop-file") {
        stop_file = std::filesystem::path(value);
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
      } else if (name == "--table-storage") {
        if (value == "double") {
          config.storage = pb::TableStorage::Double;
        } else if (value == "mixed") {
          config.storage = pb::TableStorage::MixedFloatSums;
        } else if (value == "float32") {
          config.storage = pb::TableStorage::Float32;
        } else {
          throw std::runtime_error("unknown table storage " + std::string{value});
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
    if (!board_texture_path.empty() && !use_board_class_rows) {
      throw std::runtime_error("--board-texture-map requires --board-class-rows");
    }
    if (resume && (checkpoint_path.empty() || !std::filesystem::exists(checkpoint_path))) {
      throw std::runtime_error("--resume requires an existing checkpoint");
    }
    if (checkpoint_every > 0U && checkpoint_path.empty()) {
      throw std::runtime_error("--checkpoint-every requires --checkpoint");
    }
    if ((chart_every > 0U) != !chart_dir.empty()) {
      throw std::runtime_error("--chart-every and --chart-dir go together");
    }
    if (policy_snapshots && chart_every == 0U) {
      throw std::runtime_error("--policy-snapshots requires --chart-every and --chart-dir");
    }
    if (policy_snapshot_every > 0U &&
        (!policy_snapshots || policy_snapshot_every % chart_every != 0U)) {
      throw std::runtime_error(
          "--policy-snapshot-every requires --policy-snapshots and a multiple of --chart-every");
    }
    if (!(policy_snapshot_reserve_gb >= 0.0 && policy_snapshot_reserve_gb <= 1.0e9))
      throw std::runtime_error("--policy-snapshot-reserve-gb must be in [0, 1e9]");
    if (!stop_file.empty() && std::filesystem::exists(stop_file)) {
      // A stop file left by an earlier run would end this one after one iteration.
      std::filesystem::remove(stop_file);
      std::cerr << "removed the stale stop file " << stop_file.string() << '\n';
    }
    if (!(target_pot_percent > 0.0 && target_pot_percent <= 100.0))
      throw std::runtime_error("--target-pot-percent must be in (0, 100]");
    if (automatic_target) {
      if (!batch_explicit)
        config.batch_boards = 32U;
      if (!threads_explicit) {
        const auto hardware_threads = std::max(1U, std::thread::hardware_concurrency());
        config.threads = std::min(8U, hardware_threads);
        config.evaluation_threads = std::min(8U, hardware_threads);
      }
      if (config.partition_target_nodes == 0U)
        config.partition_target_nodes = 64U;
      config.batch_policy_refresh = true;
      config.lazy_discount = true;
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
    if (chart_every > 0U && compiled.value().config().player_count != 2U) {
      throw std::runtime_error("--chart-every writes heads-up charts only");
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
    std::optional<pb::ClassBucketRows> class_rows;
    std::optional<pb::HistoryBucketRows> history_rows;
    if (!history_rows_path.empty()) {
      if (use_class_rows)
        throw std::runtime_error("choose class rows or history rows");
      auto mapped = pb::HistoryBucketRows::load(history_rows_path);
      if (!mapped)
        throw std::runtime_error("history map load failed");
      history_rows.emplace(std::move(mapped.value()));
      resources.history_rows = &*history_rows;
      config.flop_capacity = history_rows->count(ca::BucketStreet::Flop);
      config.turn_capacity = history_rows->count(ca::BucketStreet::Turn);
      config.river_capacity = history_rows->count(ca::BucketStreet::River);
    }
    if (use_class_rows) {
      auto built = pb::ClassBucketRows::build(flop.value(), turn.value(), river.value());
      if (!built) {
        throw std::runtime_error("class row mapping failed");
      }
      class_rows.emplace(std::move(built.value()));
      resources.class_rows = &*class_rows;
      config.flop_capacity = class_rows->count(ca::BucketStreet::Flop);
      config.turn_capacity = class_rows->count(ca::BucketStreet::Turn);
      config.river_capacity = class_rows->count(ca::BucketStreet::River);
    }
    std::optional<pb::BoardClassRows> board_class_rows;
    if (use_board_class_rows) {
      if (use_class_rows || history_rows)
        throw std::runtime_error("choose one of class rows, history rows, board class rows");
      // The best response reads board class rows, but the certificate does
      // not record them and the in-training evaluation of this CLI has not
      // been validated with them: evaluate the saved policy separately
      // (preflop_blueprint_monker_values).
      if (evaluate_every > 0U || !certificate_path.empty())
        throw std::runtime_error("--board-class-rows requires --eval-every 0 and no certificate");
      auto texture = pb::BoardTextureMap::identity();
      if (!board_texture_path.empty()) {
        auto loaded_texture = pb::BoardTextureMap::load(board_texture_path, catalog);
        if (!loaded_texture) {
          throw std::runtime_error("board texture map " + board_texture_path.string() +
                                   " rejected: " +
                                   pb::texture_error_name(loaded_texture.error()));
        }
        texture = std::move(loaded_texture.value());
      }
      board_class_rows.emplace(flop.value().capacity(), turn.value().capacity(),
                               river.value().capacity(), std::move(texture));
      resources.board_class_rows = &*board_class_rows;
      config.flop_capacity = board_class_rows->count(ca::BucketStreet::Flop);
      config.turn_capacity = board_class_rows->count(ca::BucketStreet::Turn);
      config.river_capacity = board_class_rows->count(ca::BucketStreet::River);
    }
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
    // Board texture of the board class rows (null without them); the
    // identity has an empty fingerprint.
    nlohmann::json board_texture_json = nullptr;
    if (board_class_rows) {
      const auto &texture = board_class_rows->texture();
      board_texture_json = nlohmann::json{
          {"name", texture.name()},
          {"fingerprint", texture.fingerprint()},
          {"classes", nlohmann::json::array({texture.classes(ca::BucketStreet::Flop),
                                             texture.classes(ca::BucketStreet::Turn),
                                             texture.classes(ca::BucketStreet::River)})}};
    }
    const double preparation_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    std::cout << "{\"event\": \"start\", \"config_id\": \"" << game_config.value().id
              << "\", \"tree_fingerprint\": \"" << compiled.value().fingerprint()
              << "\", \"trainer_identity\": \"" << trainer.identity()
              << "\", \"nodes\": " << stats.node_count << ", \"decisions\": " << stats.decision_nodes
              << ", \"capacities\": [" << config.flop_capacity << ", " << config.turn_capacity
              << ", " << config.river_capacity << "], \"board_texture\": "
              << board_texture_json.dump() << ", \"state_bytes\": " << trainer.state_bytes()
              << ", \"history_map_resident_bytes\": "
              << (history_rows ? history_rows->resident_byte_size() : 0U)
              << ", \"units\": " << trainer.partition().unit_roots.size()
              << ", \"top_nodes\": " << trainer.partition().top_nodes
              << ", \"largest_unit\": " << trainer.partition().largest_unit_nodes
              << ", \"batch\": " << config.batch_boards << ", \"threads\": " << config.threads
              << ", \"evaluation_threads\": "
              << (config.evaluation_threads == 0U ? config.threads : config.evaluation_threads)
              << ", \"scheme\": \"" << pb::weighting_scheme_name(config.scheme)
              << "\", \"lazy_discount\": " << (config.lazy_discount ? "true" : "false")
              << ", \"reuse_discount_invariant_policy\": "
              << (config.reuse_discount_invariant_policy ? "true" : "false")
              << ", \"prefetch_refresh_rows\": " << config.prefetch_refresh_rows
              << ", \"prefetch_update_hands\": " << config.prefetch_update_hands
              << ", \"lazy_discount_epoch\": " << config.lazy_discount_epoch
              << ", \"table_storage\": \"" << pb::table_storage_name(config.storage)
              << "\", \"policy_storage\": \"compact-batch-v1\""
              << ", \"update\": \"" << pb::update_mode_name(config.update_mode)
              << "\", \"resumed_iteration\": " << trainer.iteration()
              << ", \"initial_pot_antes\": " << trainer.initial_pot_antes()
              << ", \"target_pot_percent\": " << target_pot_percent
              << ", \"automatic_target\": " << (automatic_target ? "true" : "false")
              << ", \"preparation_seconds\": " << preparation_seconds << "}\n";
    {
      auto breakdown = memory_breakdown_json(trainer.memory_breakdown());
      breakdown["process"] = peaks_json(pb::process_memory_peaks());
      breakdown["stage"] = "after_initialization";
      std::cout << breakdown.dump() << "\n" << std::flush;
    }

    // Source text of the exported policies (the evaluators check the abstraction suffix).
    const std::string abstraction_source =
        history_rows ? "|abstraction=" + std::string(history_rows->format_name()) +
                           "|map=" + history_rows->fingerprint()
        : use_class_rows
            ? "|abstraction=class-major-v1|flop=" + flop.value().fingerprint() +
                  "|turn=" + turn.value().fingerprint() + "|river=" + river.value().fingerprint()
        : board_class_rows
            ? "|abstraction=" + board_class_rows->fingerprint() + "|flop=" +
                  flop.value().fingerprint() + "|turn=" + turn.value().fingerprint() +
                  "|river=" + river.value().fingerprint()
            : "";
    const auto average_policy_source = [&] {
      return trainer.identity() + "|iteration=" + std::to_string(trainer.iteration()) +
             abstraction_source;
    };
    double training_seconds = 0.0;
    const auto initial_iteration = trainer.iteration();
    double discount_seconds = 0.0, refresh_seconds = 0.0, prepare_seconds = 0.0,
           traversal_seconds = 0.0;
    pb::IterationTelemetry detailed_profile;
    std::array<std::uint64_t, 4> rows_materialized{};
    std::array<std::uint64_t, 4> cells_materialized{};
    std::array<std::uint64_t, 4> hand_lookups{};
    std::uint64_t compact_policy_bytes_peak = 0U;
    double evaluation_seconds = 0.0;
    double certification_seconds = 0.0;
    double write_seconds = 0.0;
    double refresh_collect_seconds = 0.0, refresh_materialize_seconds = 0.0;
    bool converged = false;
    bool certified_exact = false;
    bool plateau = false;
    std::uint64_t process_bytes = 0U;
    // A loaded checkpoint already represents the current iteration. Avoid
    // rewriting the same multi-gigabyte file in evaluation-only/export runs.
    std::optional<std::uint64_t> saved_checkpoint_iteration =
        resume ? std::optional<std::uint64_t>{trainer.iteration()} : std::nullopt;
    const auto save_checkpoint_once = [&](const std::string_view failure) {
      if (checkpoint_path.empty() || saved_checkpoint_iteration == trainer.iteration())
        return;
      const auto write_started = Clock::now();
      const auto saved = trainer.save_checkpoint(checkpoint_path);
      write_seconds += std::chrono::duration<double>(Clock::now() - write_started).count();
      if (!saved)
        throw std::runtime_error(std::string(failure));
      saved_checkpoint_iteration = trainer.iteration();
    };
    constexpr std::uint64_t first_automatic_checkpoint = 250U;
    auto next_automatic_checkpoint = first_automatic_checkpoint;
    while (next_automatic_checkpoint <= trainer.iteration() &&
           next_automatic_checkpoint <= std::numeric_limits<std::uint64_t>::max() / 2U)
      next_automatic_checkpoint *= 2U;
    std::vector<double> automatic_upper_bounds;
    if (!coverage_path.empty()) {
      nlohmann::json report{{"schema", "gtosd.research.trainer_coverage.v1"},
                            {"iteration", trainer.iteration()},
                            {"boards_processed", trainer.boards_processed()},
                            {"tree_fingerprint", compiled.value().fingerprint()},
                            {"trainer_identity", trainer.identity()}};
      nlohmann::json nodes = nlohmann::json::array();
      std::array<std::uint64_t, 4> possible_by_street{};
      std::array<std::uint64_t, 4> strategy_by_street{};
      std::array<std::uint64_t, 4> regret_by_street{};
      const auto &layout = trainer.layout();
      for (const auto &node : compiled.value().nodes()) {
        if (node.kind != pb::NodeKind::Decision)
          continue;
        const auto street = static_cast<std::size_t>(node.street);
        const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                    layout.turn_capacity, layout.river_capacity);
        std::uint64_t strategy_rows = 0U;
        std::uint64_t regret_rows = 0U;
        const auto base = layout.offsets[node.id];
        for (std::uint32_t row = 0; row < rows; ++row) {
          const auto offset = base + static_cast<std::uint64_t>(row) * node.action_count;
          bool has_strategy = false;
          bool has_regret = false;
          for (std::uint8_t action = 0; action < node.action_count; ++action) {
            has_strategy = has_strategy || trainer.strategy_sum(offset + action) != 0.0;
            has_regret = has_regret || trainer.regret(offset + action) != 0.0;
          }
          strategy_rows += has_strategy ? 1U : 0U;
          regret_rows += has_regret ? 1U : 0U;
        }
        possible_by_street[street] += rows;
        strategy_by_street[street] += strategy_rows;
        regret_by_street[street] += regret_rows;
        nodes.push_back({{"node", node.id},
                         {"street", pb::street_name(node.street)},
                         {"actor", node.actor},
                         {"actions", node.action_count},
                         {"rows", rows},
                         {"strategy_rows", strategy_rows},
                         {"regret_rows", regret_rows},
                         {"strategy_fraction", static_cast<double>(strategy_rows) / rows},
                         {"regret_fraction", static_cast<double>(regret_rows) / rows}});
      }
      nlohmann::json streets = nlohmann::json::array();
      for (std::size_t street = 0; street < possible_by_street.size(); ++street) {
        const auto possible = possible_by_street[street];
        streets.push_back(
            {{"street", pb::street_name(static_cast<gtosd::Street>(street))},
             {"rows", possible},
             {"strategy_rows", strategy_by_street[street]},
             {"regret_rows", regret_by_street[street]},
             {"strategy_fraction",
              possible == 0U ? 0.0 : static_cast<double>(strategy_by_street[street]) / possible},
             {"regret_fraction",
              possible == 0U ? 0.0 : static_cast<double>(regret_by_street[street]) / possible}});
      }
      report["streets"] = std::move(streets);
      report["nodes"] = std::move(nodes);
      std::ofstream output(coverage_path, std::ios::binary | std::ios::trunc);
      output << report.dump(2) << '\n';
      if (!output)
        throw std::runtime_error("coverage report write failed");
      std::cout << "{\"event\":\"coverage\",\"output\":\""
                << coverage_path.generic_string() << "\"}\n";
      automatic_target = false;
      iterations = trainer.iteration();
    } else if (evaluate_only) {
      const auto estimate = trainer.estimate_exploitability(evaluation_flops);
      if (!estimate) {
        throw std::runtime_error(std::string("evaluation failed: ") +
                                 pb::trainer_error_name(estimate.error()));
      }
      evaluation_seconds += estimate.value().seconds;
      print_estimate(trainer.iteration(),
                     std::chrono::duration<double>(Clock::now() - started).count(),
                     estimate.value(), pb::process_working_set_bytes(), trainer.boards_processed());
      converged =
          estimate.value().meets_stop_rule(trainer.initial_pot_antes(), target_pot_percent);
      automatic_target = false;
      iterations = trainer.iteration();
    }
    bool breakdown_after_first_iteration = false;
    bool stopped_by_file = false;
    while ((automatic_target || trainer.iteration() < iterations) && !converged && !plateau) {
      const auto telemetry = trainer.iterate();
      if (!telemetry) {
        throw std::runtime_error(std::string("iteration failed: ") +
                                 pb::trainer_error_name(telemetry.error()));
      }
      training_seconds += telemetry.value().seconds;
      discount_seconds += telemetry.value().discount_seconds;
      refresh_seconds += telemetry.value().policy_refresh_seconds;
      refresh_collect_seconds += telemetry.value().policy_refresh_collect_seconds;
      refresh_materialize_seconds += telemetry.value().policy_refresh_materialize_seconds;
      prepare_seconds += telemetry.value().board_prepare_seconds;
      traversal_seconds += telemetry.value().traversal_seconds;
      for (std::size_t street = 0; street < 4U; ++street) {
        rows_materialized[street] += telemetry.value().policy_rows_materialized[street];
        cells_materialized[street] += telemetry.value().policy_cells_materialized[street];
        hand_lookups[street] += telemetry.value().policy_hand_lookups[street];
      }
      compact_policy_bytes_peak =
          std::max(compact_policy_bytes_peak, telemetry.value().compact_policy_bytes);
      detailed_profile.traversal_weight_setup_seconds +=
          telemetry.value().traversal_weight_setup_seconds;
      detailed_profile.board_context_cpu_seconds +=
          telemetry.value().board_context_cpu_seconds;
      detailed_profile.all_in_cache_cpu_seconds +=
          telemetry.value().all_in_cache_cpu_seconds;
      detailed_profile.reach_setup_cpu_seconds +=
          telemetry.value().reach_setup_cpu_seconds;
      detailed_profile.traversal_top_down_seconds +=
          telemetry.value().traversal_top_down_seconds;
      detailed_profile.traversal_parallel_seconds +=
          telemetry.value().traversal_parallel_seconds;
      detailed_profile.traversal_top_reduce_seconds +=
          telemetry.value().traversal_top_reduce_seconds;
      detailed_profile.nodes_visited += telemetry.value().nodes_visited;
      detailed_profile.decision_nodes_visited += telemetry.value().decision_nodes_visited;
      detailed_profile.hero_decision_nodes += telemetry.value().hero_decision_nodes;
      detailed_profile.opponent_decision_nodes += telemetry.value().opponent_decision_nodes;
      detailed_profile.chance_nodes_visited += telemetry.value().chance_nodes_visited;
      detailed_profile.fold_terminals_visited += telemetry.value().fold_terminals_visited;
      detailed_profile.preflop_all_in_terminals_visited +=
          telemetry.value().preflop_all_in_terminals_visited;
      detailed_profile.postflop_showdown_terminals_visited +=
          telemetry.value().postflop_showdown_terminals_visited;
      detailed_profile.zero_reach_prunes += telemetry.value().zero_reach_prunes;
      detailed_profile.policy_rows_read += telemetry.value().policy_rows_read;
      detailed_profile.regret_cells_written += telemetry.value().regret_cells_written;
      detailed_profile.strategy_cells_written += telemetry.value().strategy_cells_written;
      detailed_profile.sampled_hero_reach_seconds +=
          telemetry.value().sampled_hero_reach_seconds;
      detailed_profile.sampled_hero_update_seconds +=
          telemetry.value().sampled_hero_update_seconds;
      detailed_profile.sampled_opponent_reach_seconds +=
          telemetry.value().sampled_opponent_reach_seconds;
      detailed_profile.sampled_opponent_accumulate_seconds +=
          telemetry.value().sampled_opponent_accumulate_seconds;
      detailed_profile.sampled_fold_terminal_seconds +=
          telemetry.value().sampled_fold_terminal_seconds;
      detailed_profile.sampled_preflop_all_in_seconds +=
          telemetry.value().sampled_preflop_all_in_seconds;
      detailed_profile.sampled_postflop_showdown_seconds +=
          telemetry.value().sampled_postflop_showdown_seconds;
      process_bytes = telemetry.value().process_bytes;
      if (!breakdown_after_first_iteration) {
        breakdown_after_first_iteration = true;
        auto breakdown = memory_breakdown_json(trainer.memory_breakdown());
        breakdown["process"] = peaks_json(pb::process_memory_peaks());
        breakdown["stage"] = "after_first_iteration";
        std::cout << breakdown.dump() << "\n" << std::flush;
      }
      if (progress_every > 0U && trainer.iteration() % progress_every == 0U) {
        std::cout << "{\"event\":\"training_progress\",\"iteration\":" << trainer.iteration()
                  << ",\"training_seconds\":" << training_seconds
                  << ",\"discount_seconds\":" << discount_seconds
                  << ",\"policy_refresh_seconds\":" << refresh_seconds
                  << ",\"board_prepare_seconds\":" << prepare_seconds
                  << ",\"traversal_seconds\":" << traversal_seconds
                  << ",\"boards_processed\":" << trainer.boards_processed()
                  << ",\"process_bytes\":" << process_bytes << "}\n"
                  << std::flush;
      }
      if (checkpoint_every > 0U && trainer.iteration() % checkpoint_every == 0U) {
        save_checkpoint_once("periodic checkpoint write failed");
      }
      if (chart_every > 0U && trainer.iteration() % chart_every == 0U) {
        // A failed snapshot must not end hours of training: report it and go on.
        const auto &game = compiled.value();
        const auto name = "it_" + std::to_string(trainer.iteration());
        const auto temporary = chart_dir / (name + ".tmp");
        std::string failure;
        // The policy is written inside the temporary directory, so it_<N> appears
        // with it; a failed policy is reported on its own and the charts still count.
        std::string policy_fingerprint;
        std::string policy_failure;
        double policy_seconds = 0.0;
        const bool policy_due =
            policy_snapshots &&
            (policy_snapshot_every == 0U || trainer.iteration() % policy_snapshot_every == 0U);
        bool policy_skipped = false;
        std::uint64_t space_available = 0U;
        std::uint64_t space_required = 0U;
        try {
          std::error_code error;
          std::filesystem::remove_all(temporary, error);
          gtosd::monker_charts::write_row_charts(
              game,
              [&](const std::uint32_t node, const std::uint8_t hand_class) {
                std::vector<double> row(game.nodes()[node].action_count);
                trainer.average_strategy_row(node, hand_class, row.data());
                return row;
              },
              temporary);
          if (policy_due) {
            // After this policy the run still writes the temporary copy of the next
            // checkpoint (a failed checkpoint write ends the run) and the final
            // --policy-out export; both are assumed on the chart directory's disk.
            const auto margin_bytes =
                static_cast<std::uint64_t>(policy_snapshot_reserve_gb * 1073741824.0);
            const std::uint64_t policy_bytes = trainer.layout().entries * sizeof(double);
            std::uint64_t checkpoint_bytes = 0U;
            if (!checkpoint_path.empty()) {
              std::error_code size_error;
              const auto size = std::filesystem::file_size(checkpoint_path, size_error);
              checkpoint_bytes = size_error ? trainer.state_bytes() : size;
            }
            space_required = policy_bytes + checkpoint_bytes +
                             (policy_path.empty() ? 0U : policy_bytes) + margin_bytes;
            std::error_code space_error;
            const auto space = std::filesystem::space(temporary, space_error);
            if (!space_error) {
              space_available = space.available;
              policy_skipped = space.available < space_required;
            }
          }
          if (policy_due && !policy_skipped) {
            const auto policy_started = Clock::now();
            try {
              const auto saved = trainer.save_average_policy_snapshot(temporary / "policy.bin",
                                                                      average_policy_source());
              if (saved)
                policy_fingerprint = saved.value();
              else
                policy_failure = pb::trainer_error_name(saved.error());
            } catch (const std::exception &exception) {
              policy_failure = exception.what();
            }
            policy_seconds =
                std::chrono::duration<double>(Clock::now() - policy_started).count();
          }
          // A fresh directory can be briefly held by an indexer or antivirus.
          for (int attempt = 0; attempt < 5; ++attempt) {
            error.clear();
            std::filesystem::remove_all(chart_dir / name, error);
            error.clear();
            std::filesystem::rename(temporary, chart_dir / name, error);
            if (!error)
              break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
          }
          if (error)
            failure = "rename failed: " + error.message();
        } catch (const std::exception &exception) {
          failure = exception.what();
        }
        if (failure.empty()) {
          std::cout << "{\"event\":\"charts\",\"iteration\":" << trainer.iteration()
                    << ",\"training_seconds\":" << training_seconds;
          if (policy_due && !policy_skipped && policy_failure.empty())
            std::cout << ",\"policy_fingerprint\":\"" << policy_fingerprint
                      << "\",\"policy_seconds\":" << policy_seconds;
          std::cout << "}\n" << std::flush;
          if (policy_skipped)
            std::cout << "{\"event\":\"policy_snapshot_skipped\",\"iteration\":"
                      << trainer.iteration() << ",\"reason\":\"disk_space\""
                      << ",\"available_bytes\":" << space_available
                      << ",\"required_bytes\":" << space_required << "}\n"
                      << std::flush;
          if (!policy_failure.empty())
            std::cout << "{\"event\":\"policy_snapshot_failed\",\"iteration\":"
                      << trainer.iteration()
                      << ",\"error\":" << nlohmann::json(policy_failure).dump() << "}\n"
                      << std::flush;
        } else {
          std::cout << "{\"event\":\"charts_failed\",\"iteration\":" << trainer.iteration()
                    << ",\"error\":" << nlohmann::json(failure).dump() << "}\n"
                    << std::flush;
        }
      }
      std::error_code stop_error;
      if (!stop_file.empty() && std::filesystem::exists(stop_file, stop_error)) {
        stopped_by_file = true;
        std::cout << "{\"event\":\"stop_file\",\"iteration\":" << trainer.iteration()
                  << "}\n"
                  << std::flush;
        break;
      }
      const bool evaluate = automatic_target
                                ? trainer.iteration() == next_automatic_checkpoint
                                : evaluate_every > 0U &&
                                      (trainer.iteration() % evaluate_every == 0U ||
                                       trainer.iteration() == iterations);
      if (!evaluate) {
        continue;
      }
      if (automatic_target && !checkpoint_path.empty()) {
        save_checkpoint_once("pre-evaluation checkpoint write failed");
      }
      if (automatic_target)
        trainer.reseed_evaluation(config.evaluation_seed);
      const auto estimate = trainer.estimate_exploitability(evaluation_flops);
      if (!estimate) {
        throw std::runtime_error(std::string("evaluation failed: ") +
                                 pb::trainer_error_name(estimate.error()));
      }
      evaluation_seconds += estimate.value().seconds;
      print_estimate(trainer.iteration(),
                     std::chrono::duration<double>(Clock::now() - started).count(),
                     estimate.value(), process_bytes, trainer.boards_processed(),
                     automatic_target ? "screen" : "scheduled");
      if (automatic_target) {
        const auto target = 0.01 * target_pot_percent * trainer.initial_pot_antes();
        automatic_upper_bounds.push_back(estimate.value().max_gain +
                                         estimate.value().max_gain_half_width);
        // Run the authoritative exact pass once either the point estimate is
        // close enough or the conservative lower endpoint is within 2x the
        // target. The latter avoids starving exact certification when the
        // maximum over a small flop sample has a persistent upward bias.
        const bool certification_candidate =
            estimate.value().max_gain <= 4.0 * target ||
            estimate.value().max_gain_lower <= 2.0 * target;
        if (certification_candidate) {
            // The exact pass needs the average strategy as a dense table for
            // its duration (one table of layout().entries doubles).
            const auto average = trainer.average_policy();
            pb::BestResponseResources certification_resources;
            certification_resources.ranks = resources.ranks;
            certification_resources.all_in = resources.all_in;
            certification_resources.catalog = resources.catalog;
            certification_resources.flop = resources.flop;
            certification_resources.turn = resources.turn;
            certification_resources.river = resources.river;
            certification_resources.class_rows = resources.class_rows;
            certification_resources.history_rows = resources.history_rows;
            pb::CertifierOptions options;
            options.threads = config.evaluation_threads == 0U ? config.threads
                                                               : config.evaluation_threads;
            options.progress = [](const pb::CertifierProgress &progress) {
              std::cout << "{\"event\":\"exact_certification_progress\",\"flops_done\":"
                        << progress.flops_done << ",\"flops_total\":" << progress.flops_total
                        << ",\"boards_done\":" << progress.boards_done
                        << ",\"elapsed_seconds\":" << progress.seconds << "}\n"
                        << std::flush;
            };
            const auto certificate = pb::certify(compiled.value(), average,
                                                 certification_resources, options);
            if (!certificate)
              throw std::runtime_error(std::string("exact certification failed: ") +
                                       pb::certifier_error_name(certificate.error()));
            certification_seconds += certificate.value().seconds;
            const auto exact_target =
                0.01 * target_pot_percent * certificate.value().initial_pot_antes;
            certified_exact = certificate.value().exact && !certificate.value().partial;
            converged = certified_exact && certificate.value().report.max_gain <= exact_target;
            auto certificate_output =
                nlohmann::json::parse(pb::certificate_json(certificate.value()));
            certificate_output["target_pot_percent"] = target_pot_percent;
            certificate_output["target_antes"] = exact_target;
            certificate_output["passes_target"] = converged;
            if (!certificate_path.empty()) {
              std::ofstream output(certificate_path, std::ios::binary | std::ios::trunc);
              output << certificate_output.dump(2) << '\n';
              if (!output)
                throw std::runtime_error("exact certificate write failed");
            }
            std::cout << "{\"event\":\"exact_certification\",\"iteration\":"
                      << trainer.iteration() << ",\"max_gain\":"
                      << certificate.value().report.max_gain << ",\"target_antes\":"
                      << exact_target << ",\"passes_target\":"
                      << (converged ? "true" : "false") << "}\n";
        }
        if (!converged && automatic_upper_bounds.size() >= 4U) {
          const auto begin = automatic_upper_bounds.end() - 4;
          const auto best_later = *std::min_element(begin + 1, automatic_upper_bounds.end());
          const auto improvement = (*begin - best_later) / std::max(*begin, target);
          plateau = best_later > target && improvement < 0.05;
        }
        if (!converged && !plateau) {
          if (next_automatic_checkpoint > std::numeric_limits<std::uint64_t>::max() / 2U)
            throw std::runtime_error("automatic checkpoint schedule overflow");
          next_automatic_checkpoint *= 2U;
        }
      } else {
        converged =
            estimate.value().meets_stop_rule(trainer.initial_pot_antes(), target_pot_percent);
      }
      if (!checkpoint_path.empty()) {
        save_checkpoint_once("checkpoint write failed");
      }
    }
    const auto training_end_peaks = pb::process_memory_peaks();
    std::string policy_fingerprint_text;
    // Save final state even when periodic evaluation was explicitly disabled. The
    // pending discounts are materialized once here, after which every save is a
    // read-only pass: the checkpoint (raw copy of the tables) is written by a helper
    // thread while the policy exports stream on this one.
    // Row coverage of the lazy discount (rows ever materialized and the 4 KiB regret
    // pages they span); meaningful unless the run was resumed from a checkpoint.
    const auto row_coverage = trainer.row_coverage();
    trainer.materialize_discounts();
    struct ThreadJoiner {
      std::thread &thread;
      ~ThreadJoiner() {
        if (thread.joinable())
          thread.join();
      }
    };
    std::thread checkpoint_thread;
    ThreadJoiner checkpoint_joiner{checkpoint_thread};
    std::string checkpoint_failure;
    const auto overlapped_write_started = Clock::now();
    if (!checkpoint_path.empty()) {
      checkpoint_thread = std::thread([&] {
        const auto saved = trainer.save_checkpoint(checkpoint_path);
        if (!saved)
          checkpoint_failure = std::string("final checkpoint write failed: ") +
                               pb::trainer_error_name(saved.error());
      });
    }
    if (!current_policy_path.empty()) {
      const auto saved = trainer.save_current_policy(
          current_policy_path,
          trainer.identity() + "|current|iteration=" + std::to_string(trainer.iteration()) +
              abstraction_source);
      if (!saved) {
        throw std::runtime_error("current policy write failed");
      }
    }
    if (!policy_path.empty()) {
      const auto saved = trainer.save_average_policy(policy_path, average_policy_source());
      if (!saved) {
        throw std::runtime_error(std::string("policy write failed: ") +
                                 pb::trainer_error_name(saved.error()));
      }
      policy_fingerprint_text = saved.value();
    }
    if (checkpoint_thread.joinable())
      checkpoint_thread.join();
    if (!checkpoint_failure.empty())
      throw std::runtime_error(checkpoint_failure);
    write_seconds +=
        std::chrono::duration<double>(Clock::now() - overlapped_write_started).count();
    const auto final_peaks = pb::process_memory_peaks();
    std::cout << "{\"event\": \"end\", \"iteration\": " << trainer.iteration()
              << ", \"policy_fingerprint\": \"" << policy_fingerprint_text << "\""
              << ", \"boards_processed\": " << trainer.boards_processed()
              << ", \"boards_distinct\": " << trainer.boards_distinct()
              << ", \"boards_repeated\": " << trainer.boards_repeated()
              << ", \"rows_total\": " << array_text(row_coverage.rows_total)
              << ", \"rows_touched\": " << array_text(row_coverage.rows_touched)
              << ", \"regret_pages_total\": " << array_text(row_coverage.regret_pages_total)
              << ", \"regret_pages_touched\": " << array_text(row_coverage.regret_pages_touched)
              << ", \"converged\": " << (converged ? "true" : "false")
              << ", \"plateau\": " << (plateau ? "true" : "false")
              << ", \"convergence_status\": \""
              << (converged && certified_exact
                      ? "CERTIFIED_EXACT"
                      : plateau ? "PLATEAU"
                                : certified_exact ? "CERTIFIED_FAIL"
                                                  : converged ? "ESTIMATED" : "NOT_REACHED")
              << "\""
              << ", \"target_pot_percent\": " << target_pot_percent
              << ", \"preparation_seconds\": " << preparation_seconds
              << ", \"training_seconds\": " << training_seconds
              << ", \"evaluation_seconds\": " << evaluation_seconds
              << ", \"certification_seconds\": " << certification_seconds
              << ", \"write_seconds\": " << write_seconds
              << ", \"discount_seconds\": " << discount_seconds
              << ", \"policy_refresh_seconds\": " << refresh_seconds
              << ", \"policy_refresh_collect_seconds\": " << refresh_collect_seconds
              << ", \"policy_refresh_materialize_seconds\": " << refresh_materialize_seconds
              << ", \"board_prepare_seconds\": " << prepare_seconds
              << ", \"traversal_seconds\": " << traversal_seconds
              << ", \"policy_cells\": {\"rows_materialized\":[" << rows_materialized[0] << ","
              << rows_materialized[1] << "," << rows_materialized[2] << ","
              << rows_materialized[3] << "],\"cells_materialized\":[" << cells_materialized[0]
              << "," << cells_materialized[1] << "," << cells_materialized[2] << ","
              << cells_materialized[3] << "],\"hand_lookups\":[" << hand_lookups[0] << ","
              << hand_lookups[1] << "," << hand_lookups[2] << "," << hand_lookups[3]
              << "],\"compact_policy_bytes_peak\":" << compact_policy_bytes_peak
              << ",\"policy_rows_read_profiled\":" << detailed_profile.policy_rows_read << "}"
              << ", \"traversal_profile\": {\"enabled\":"
              << (config.detailed_profile ? "true" : "false")
              << ",\"board_context_cpu_seconds\":"
              << detailed_profile.board_context_cpu_seconds
              << ",\"all_in_cache_cpu_seconds\":"
              << detailed_profile.all_in_cache_cpu_seconds
              << ",\"reach_setup_cpu_seconds\":"
              << detailed_profile.reach_setup_cpu_seconds
              << ",\"weight_setup_seconds\":"
              << detailed_profile.traversal_weight_setup_seconds
              << ",\"top_down_seconds\":" << detailed_profile.traversal_top_down_seconds
              << ",\"parallel_seconds\":" << detailed_profile.traversal_parallel_seconds
              << ",\"top_reduce_seconds\":"
              << detailed_profile.traversal_top_reduce_seconds
              << ",\"nodes\":" << detailed_profile.nodes_visited
              << ",\"decisions\":" << detailed_profile.decision_nodes_visited
              << ",\"hero_decisions\":" << detailed_profile.hero_decision_nodes
              << ",\"opponent_decisions\":" << detailed_profile.opponent_decision_nodes
              << ",\"chance\":" << detailed_profile.chance_nodes_visited
              << ",\"fold_terminals\":" << detailed_profile.fold_terminals_visited
              << ",\"preflop_all_in_terminals\":"
              << detailed_profile.preflop_all_in_terminals_visited
              << ",\"postflop_showdown_terminals\":"
              << detailed_profile.postflop_showdown_terminals_visited
              << ",\"zero_reach_prunes\":" << detailed_profile.zero_reach_prunes
              << ",\"policy_rows_read\":" << detailed_profile.policy_rows_read
              << ",\"regret_cells_written\":" << detailed_profile.regret_cells_written
              << ",\"strategy_cells_written\":"
              << detailed_profile.strategy_cells_written
              << ",\"sample_stride\":1024"
              << ",\"sampled_hero_reach_seconds\":"
              << detailed_profile.sampled_hero_reach_seconds
              << ",\"sampled_hero_update_seconds\":"
              << detailed_profile.sampled_hero_update_seconds
              << ",\"sampled_opponent_reach_seconds\":"
              << detailed_profile.sampled_opponent_reach_seconds
              << ",\"sampled_opponent_accumulate_seconds\":"
              << detailed_profile.sampled_opponent_accumulate_seconds
              << ",\"sampled_fold_terminal_seconds\":"
              << detailed_profile.sampled_fold_terminal_seconds
              << ",\"sampled_preflop_all_in_seconds\":"
              << detailed_profile.sampled_preflop_all_in_seconds
              << ",\"sampled_postflop_showdown_seconds\":"
              << detailed_profile.sampled_postflop_showdown_seconds << "}"
              << ", \"seconds_per_iteration\": "
              << (trainer.iteration() > initial_iteration
                      ? training_seconds /
                            static_cast<double>(trainer.iteration() - initial_iteration)
                      : 0.0)
              << ", \"state_fingerprint\": \"" << trainer.state_fingerprint()
              << "\", \"process_bytes\": " << pb::process_working_set_bytes()
              << ", \"process_after_training\": " << peaks_json(training_end_peaks).dump()
              << ", \"process_final\": " << peaks_json(final_peaks).dump()
              << ", \"total_seconds\": "
              << std::chrono::duration<double>(Clock::now() - started).count() << "}\n";
    std::cout << "PREFLOP_BLUEPRINT_TRAIN="
              << (converged           ? "CONVERGED"
                  : plateau           ? "PLATEAU"
                  : stopped_by_file   ? "STOPPED"
                                      : "ITERATION_LIMIT")
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAIN=FAIL " << error.what() << '\n';
    return 1;
  }
}
