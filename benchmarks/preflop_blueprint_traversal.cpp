// Times the value traversal of the compiled game on sampled boards: board
// context, all-in equity cache, policy traversal for both players and the
// best-response traversal. Resources are loaded from --resources-dir (rank
// and all-in tables written by gtosd_preflop_blueprint_resources) or built;
// bucket tables from --buckets-dir select the bucket policy, otherwise a
// per-hand uniform policy is used.

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

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

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path config_path;
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::uint32_t boards = 20U;
    std::uint64_t seed = 0x5052'4546'4c4f'5035ULL;
    unsigned threads = 8U;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--config") {
        config_path = value;
      } else if (name == "--resources-dir") {
        resources_dir = value;
      } else if (name == "--buckets-dir") {
        buckets_dir = value;
      } else if (name == "--boards") {
        boards = static_cast<std::uint32_t>(parse_unsigned(value));
      } else if (name == "--seed") {
        seed = parse_unsigned(value);
      } else if (name == "--threads") {
        threads = static_cast<unsigned>(parse_unsigned(value));
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    if (config_path.empty()) {
      throw std::runtime_error("--config <game fixture> is required");
    }
    const auto config = pb::parse_game_config_json(read_file(config_path));
    if (!config) {
      throw std::runtime_error(std::string("configuration rejected: ") +
                               pb::config_error_name(config.error()));
    }
    const auto compiled = pb::CompiledGame::compile(config.value());
    if (!compiled) {
      throw std::runtime_error(std::string("compile failed: ") +
                               pb::game_model_error_name(compiled.error()));
    }
    const auto &game = compiled.value();

    auto phase = Clock::now();
    std::optional<ca::RankTable> ranks;
    std::optional<ca::AllInTable> all_in;
    bool loaded = false;
    if (!resources_dir.empty()) {
      auto loaded_ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
      auto loaded_all_in = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
      if (loaded_ranks && loaded_all_in) {
        ranks.emplace(std::move(loaded_ranks.value()));
        all_in.emplace(std::move(loaded_all_in.value()));
        loaded = true;
      }
    }
    if (!loaded) {
      auto built_ranks = ca::RankTable::build();
      if (!built_ranks) {
        throw std::runtime_error("rank table build failed");
      }
      ranks.emplace(std::move(built_ranks.value()));
      auto built_all_in = ca::AllInTable::build(ranks.value(), threads);
      if (!built_all_in) {
        throw std::runtime_error("all-in table build failed");
      }
      all_in.emplace(std::move(built_all_in.value()));
    }
    std::optional<ca::BoardCatalog> catalog;
    std::optional<ca::BucketTable> flop;
    std::optional<ca::BucketTable> turn;
    std::optional<ca::BucketTable> river;
    bool buckets = false;
    if (!buckets_dir.empty()) {
      auto loaded_flop = ca::BucketTable::load(buckets_dir / "flop_buckets_v1.bin");
      auto loaded_turn = ca::BucketTable::load(buckets_dir / "turn_buckets_v1.bin");
      auto loaded_river = ca::BucketTable::load(buckets_dir / "river_buckets_v1.bin");
      if (loaded_flop && loaded_turn && loaded_river) {
        catalog.emplace(ca::BoardCatalog::build());
        flop.emplace(std::move(loaded_flop.value()));
        turn.emplace(std::move(loaded_turn.value()));
        river.emplace(std::move(loaded_river.value()));
        buckets = true;
      }
    }
    const auto preparation_seconds = seconds_since(phase);

    pb::AbstractionTables tables;
    std::unique_ptr<pb::Policy> policy;
    std::optional<pb::StateLayout> layout;
    if (buckets) {
      tables.catalog = &catalog.value();
      tables.flop = &flop.value();
      tables.turn = &turn.value();
      tables.river = &river.value();
      layout = pb::layout_state(game, flop->capacity(), turn->capacity(), river->capacity());
      policy = std::make_unique<pb::BucketPolicy>(game, layout.value());
    } else {
      policy = std::make_unique<pb::HandPolicy>(game);
    }
    const pb::HeadsUpShowdownKernel kernel;

    ca::DeterministicRandom random(seed);
    double context_seconds = 0.0;
    double cache_seconds = 0.0;
    double traversal_seconds = 0.0;
    double best_response_seconds = 0.0;
    std::uint64_t nodes_visited = 0U;
    std::uint64_t terminals = 0U;
    std::uint64_t pruned = 0U;
    double checksum = 0.0;
    std::array<double, pb::live_hand_count> reach{};
    reach.fill(1.0);
    std::array<double, pb::live_hand_count> values{};
    for (std::uint32_t board = 0; board < boards; ++board) {
      std::array<std::uint8_t, 5> cards{};
      std::uint64_t mask = 0U;
      for (auto &card : cards) {
        do {
          card = static_cast<std::uint8_t>(random.uniform_below(36U));
        } while (((mask >> card) & 1U) != 0U);
        mask |= std::uint64_t{1} << card;
      }
      std::sort(cards.begin(), cards.begin() + 3);
      ca::BoardHistory history;
      for (std::size_t index = 0; index < 3U; ++index) {
        history.flop[index] = gtosd::CardId::from_index(cards[index]).value();
      }
      history.turn = gtosd::CardId::from_index(cards[3]).value();
      history.river = gtosd::CardId::from_index(cards[4]).value();

      phase = Clock::now();
      const auto context = pb::BoardContext::build(history, ranks.value(), buckets ? &tables : nullptr);
      if (!context) {
        throw std::runtime_error(std::string("board context failed: ") +
                                 pb::kernel_error_name(context.error()));
      }
      context_seconds += seconds_since(phase);
      phase = Clock::now();
      const auto cache = pb::AllInEquityCache::build(context.value(), all_in.value());
      if (!cache) {
        throw std::runtime_error("all-in cache failed");
      }
      cache_seconds += seconds_since(phase);

      pb::ValueTraversal traversal(game, context.value(), kernel, &cache.value());
      for (std::uint8_t hero = 0; hero < 2U; ++hero) {
        phase = Clock::now();
        const auto outcome = traversal.evaluate(*policy, hero, reach, values);
        traversal_seconds += seconds_since(phase);
        if (!outcome) {
          throw std::runtime_error(std::string("traversal failed: ") +
                                   pb::kernel_error_name(outcome.error()));
        }
        nodes_visited += traversal.counters().nodes_visited;
        terminals += traversal.counters().terminals_evaluated;
        pruned += traversal.counters().subtrees_pruned;
        for (const auto value : values) {
          checksum += value;
        }
        pb::TraversalOptions options;
        options.best_response = true;
        phase = Clock::now();
        const auto best = traversal.evaluate(*policy, hero, reach, values, options);
        best_response_seconds += seconds_since(phase);
        if (!best) {
          throw std::runtime_error("best-response traversal failed");
        }
      }
    }
    const double evaluations = 2.0 * boards;
    std::cout << "{\n"
              << "  \"schema\": \"gtosd.preflop_blueprint_traversal_report.v1\",\n"
              << "  \"config_id\": \"" << config.value().id << "\",\n"
              << "  \"tree_fingerprint\": \"" << game.fingerprint() << "\",\n"
              << "  \"resources\": \"" << (loaded ? "loaded" : "built") << "\",\n"
              << "  \"policy\": \"" << (buckets ? "bucket_uniform" : "hand_uniform") << "\",\n"
              << "  \"boards\": " << boards << ", \"seed\": " << seed << ",\n"
              << "  \"preparation_seconds\": " << preparation_seconds << ",\n"
              << "  \"context_ms_per_board\": " << context_seconds / boards * 1000.0 << ",\n"
              << "  \"all_in_cache_ms_per_board\": " << cache_seconds / boards * 1000.0 << ",\n"
              << "  \"traversal_ms_per_evaluation\": " << traversal_seconds / evaluations * 1000.0
              << ",\n"
              << "  \"best_response_ms_per_evaluation\": "
              << best_response_seconds / evaluations * 1000.0 << ",\n"
              << "  \"nodes_visited_per_evaluation\": " << nodes_visited / static_cast<std::uint64_t>(evaluations)
              << ", \"terminals_per_evaluation\": " << terminals / static_cast<std::uint64_t>(evaluations)
              << ", \"pruned_per_evaluation\": " << pruned / static_cast<std::uint64_t>(evaluations) << ",\n"
              << "  \"value_checksum\": " << checksum << "\n}\n";
    std::cout << "PREFLOP_BLUEPRINT_TRAVERSAL=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAVERSAL=FAIL " << error.what() << '\n';
    return 1;
  }
}
