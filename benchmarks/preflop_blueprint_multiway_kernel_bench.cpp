// K4 microbenchmark of the three-seat terminal kernels (phase 3 spec, sections 3.11 and 8.2).
//
// On random boards with dense random reach it measures microseconds per call of each terminal
// kind (three-active showdown, two-active showdown with the folded seat dead, the D3 deal mass
// of folds and of the hero-folded shortcut), next to the heads-up kernels, for the scalar and
// AVX2 paths; reports the rank-group statistics (share of live hands alone in their rank group,
// groups per board, hand-weighted mean group size, largest group), the flops per hand counted
// from the loops and the resulting GFLOP/s; and predicts the kernel seconds per iteration of a
// 3-seat trainer from the exact per-hero terminal census of the fixture's tree (alternating
// updates, one batch of boards per hero pass). Single-threaded; the prediction for a thread
// count divides by an assumed parallel speedup and adds the spec's traversal and refresh
// estimates, so it is an inference, not a measurement.
//
// Usage: gtosd_preflop_blueprint_multiway_kernel_bench --resources-dir DIR
//          [--fixture FILE] [--boards N] [--repeats R] [--batch B] [--isa both|best|scalar|avx2]
//          [--speedup LOW HIGH] [--traversal LOW HIGH] [--refresh LOW HIGH]
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"
#include "gtosd/preflop_blueprint/multiway_kernels.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;
using Vector = std::array<double, pb::live_hand_count>;

struct Options {
  std::filesystem::path resources_dir;
  std::filesystem::path fixture =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "monker" / "3WAY50_donk_rake25cap2.json";
  std::size_t boards{48U};
  std::size_t repeats{3U};
  std::size_t batch{32U};
  std::string isa{"both"};
  std::array<double, 2> speedup{3.0, 4.0};
  std::array<double, 2> traversal{0.7, 0.9};
  std::array<double, 2> refresh{0.1, 0.2};
};

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

pb::ConstHandSpan view(const Vector &vector) {
  return pb::ConstHandSpan(vector.data(), vector.size());
}
pb::HandSpan view(Vector &vector) { return pb::HandSpan(vector.data(), vector.size()); }

ca::BoardHistory random_board(ca::DeterministicRandom &random) {
  std::array<std::uint8_t, 5> cards{};
  std::uint64_t used = 0U;
  for (auto &card : cards) {
    do {
      card = static_cast<std::uint8_t>(random.uniform_below(36U));
    } while (((used >> card) & 1U) != 0U);
    used |= std::uint64_t{1} << card;
  }
  std::sort(cards.begin(), cards.begin() + 3);
  ca::BoardHistory history;
  for (std::size_t index = 0; index < 3U; ++index)
    history.flop[index] = gtosd::CardId::from_index(cards[index]).value();
  history.turn = gtosd::CardId::from_index(cards[3]).value();
  history.river = gtosd::CardId::from_index(cards[4]).value();
  return history;
}

// Kernel calls per hero pass on one board, from the tree (upper bounds: zero-reach prunes are
// not modelled).
struct Census {
  std::array<std::uint64_t, 3> three_active{};
  std::array<std::uint64_t, 3> two_active_three_way_pot{};
  std::array<std::uint64_t, 3> two_active_two_way_pot{};
  // D3 at the hero's postflop decisions with a fold (fold children and the hero-folded
  // shortcut share it) and at the postflop folds the hero wins.
  std::array<std::uint64_t, 3> deal_at_hero_fold{};
  std::array<std::uint64_t, 3> deal_at_won_fold{};
  std::uint64_t postflop_folds{0U};
  std::uint64_t three_active_terminals{0U};
  std::uint64_t two_active_terminals{0U};
};

Census census_of(const pb::CompiledGame &game) {
  Census census;
  const auto &nodes = game.nodes();
  for (const auto &node : nodes) {
    if (node.street == gtosd::Street::Preflop)
      continue;
    if (node.kind == pb::NodeKind::TerminalShowdown) {
      const auto active = std::popcount(node.active_mask);
      const auto entry = nodes[game.postflop_entries()[node.postflop_entry]];
      const bool three_way_pot = std::popcount(entry.active_mask) == 3;
      (active == 3 ? census.three_active_terminals : census.two_active_terminals) += 1U;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        if (((node.active_mask >> seat) & 1U) == 0U)
          continue;
        if (active == 3)
          ++census.three_active[seat];
        else if (three_way_pot)
          ++census.two_active_three_way_pot[seat];
        else
          ++census.two_active_two_way_pot[seat];
      }
    } else if (node.kind == pb::NodeKind::TerminalFold) {
      ++census.postflop_folds;
      const auto folder = nodes[node.parent].actor;
      const auto winners =
          static_cast<std::uint8_t>(node.active_mask & ~static_cast<std::uint8_t>(1U << folder));
      for (std::uint8_t seat = 0U; seat < 3U; ++seat)
        if (((winners >> seat) & 1U) != 0U)
          ++census.deal_at_won_fold[seat];
    } else if (node.kind == pb::NodeKind::Decision) {
      for (const auto &edge : game.edges_of(node.id)) {
        if (edge.action.type == gtosd::ActionType::Fold) {
          ++census.deal_at_hero_fold[node.actor];
          break;
        }
      }
    }
  }
  return census;
}

struct GroupStats {
  double singleton_share{0.0};
  double groups_per_board{0.0};
  double weighted_group_size{0.0};
  std::size_t largest_group{0U};
  std::vector<std::size_t> singletons_per_board;
};

GroupStats group_stats(const std::vector<pb::BoardContext> &contexts) {
  GroupStats stats;
  double singletons = 0.0;
  double groups = 0.0;
  double weighted = 0.0;
  for (const auto &context : contexts) {
    const auto starts = context.rank_group_starts();
    std::size_t board_singletons = 0U;
    for (std::size_t group = 0; group + 1U < starts.size(); ++group) {
      const std::size_t size = static_cast<std::size_t>(starts[group + 1U]) - starts[group];
      board_singletons += size == 1U ? 1U : 0U;
      weighted += static_cast<double>(size * size) / static_cast<double>(pb::live_hand_count);
      stats.largest_group = std::max(stats.largest_group, size);
    }
    groups += static_cast<double>(starts.size() - 1U);
    singletons += static_cast<double>(board_singletons);
    stats.singletons_per_board.push_back(board_singletons);
  }
  const auto boards = static_cast<double>(contexts.size());
  stats.singleton_share = singletons / (boards * static_cast<double>(pb::live_hand_count));
  stats.groups_per_board = groups / boards;
  stats.weighted_group_size = weighted / boards;
  return stats;
}

struct KindTiming {
  double three_active_us{0.0};
  double two_active_us{0.0};
  double deal_us{0.0};
  double three_active_gflops{0.0};
  double two_active_gflops{0.0};
  double deal_gflops{0.0};
  double checksum{0.0};
};

template <typename Function>
double time_calls(const std::size_t boards, const std::size_t repeats, Function &&function) {
  // One warm-up pass, then the timed passes.
  for (std::size_t board = 0; board < boards; ++board)
    function(board);
  const auto start = Clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat)
    for (std::size_t board = 0; board < boards; ++board)
      function(board);
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  return 1e6 * seconds / static_cast<double>(boards * repeats);
}

KindTiming measure(const pb::MultiwayKernelIsa isa, const std::vector<pb::BoardContext> &contexts,
                   const std::vector<std::array<Vector, 3>> &reach, const GroupStats &groups,
                   const Options &options) {
  pb::MultiwayScratch scratch(isa);
  if (scratch.isa() != isa)
    throw std::runtime_error(std::string("instruction set unavailable: ") +
                             pb::multiway_kernel_isa_name(isa));
  KindTiming timing;
  Vector values{};
  const pb::ThreeActivePayoffs three{2.0, 0.9999, 1.0, 0.6666, -1.0};
  const pb::TwoActivePayoffs two{2.0, 1.0, -1.0};
  timing.three_active_us = time_calls(contexts.size(), options.repeats, [&](const std::size_t b) {
    pb::three_active_values(contexts[b], view(reach[b][1]), view(reach[b][2]), three, view(values),
                            scratch);
    timing.checksum += values[b % pb::live_hand_count];
  });
  timing.two_active_us = time_calls(contexts.size(), options.repeats, [&](const std::size_t b) {
    pb::two_active_values(contexts[b], view(reach[b][1]), view(reach[b][2]), two, view(values),
                          scratch);
    timing.checksum += values[b % pb::live_hand_count];
  });
  timing.deal_us = time_calls(contexts.size(), options.repeats, [&](const std::size_t b) {
    pb::three_seat_deal_mass(contexts[b], view(reach[b][1]), view(reach[b][2]), view(values),
                             scratch);
    timing.checksum += values[b % pb::live_hand_count];
  });
  const auto flops = pb::multiway_kernel_flops_per_hand();
  const double hands = static_cast<double>(pb::live_hand_count);
  const double singleton_hands = groups.singleton_share * hands;
  const double three_flops = singleton_hands * flops.three_active_singleton +
                             (hands - singleton_hands) * flops.three_active_group;
  timing.three_active_gflops = three_flops / (timing.three_active_us * 1e3);
  timing.two_active_gflops = hands * flops.two_active / (timing.two_active_us * 1e3);
  timing.deal_gflops = hands * flops.deal / (timing.deal_us * 1e3);
  return timing;
}

std::array<double, 2> parse_pair(char **argv, const int index) {
  return {std::stod(argv[index]), std::stod(argv[index + 1])};
}

Options parse(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view name = argv[index];
    const auto need = [&](const int count) {
      if (index + count >= argc)
        throw std::runtime_error("missing value for " + std::string(name));
    };
    if (name == "--resources-dir") {
      need(1);
      options.resources_dir = argv[++index];
    } else if (name == "--fixture") {
      need(1);
      options.fixture = argv[++index];
    } else if (name == "--boards") {
      need(1);
      options.boards = std::stoul(argv[++index]);
    } else if (name == "--repeats") {
      need(1);
      options.repeats = std::stoul(argv[++index]);
    } else if (name == "--batch") {
      need(1);
      options.batch = std::stoul(argv[++index]);
    } else if (name == "--isa") {
      need(1);
      options.isa = argv[++index];
    } else if (name == "--speedup") {
      need(2);
      options.speedup = parse_pair(argv, index + 1);
      index += 2;
    } else if (name == "--traversal") {
      need(2);
      options.traversal = parse_pair(argv, index + 1);
      index += 2;
    } else if (name == "--refresh") {
      need(2);
      options.refresh = parse_pair(argv, index + 1);
      index += 2;
    } else {
      throw std::runtime_error("unknown argument " + std::string(name));
    }
  }
  if (options.boards == 0U || options.repeats == 0U || options.batch == 0U)
    throw std::runtime_error("--boards, --repeats and --batch must be positive");
  return options;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto options = parse(argc, argv);
    auto ranks = ca::RankTable::load(options.resources_dir / "rank_table_v1.bin");
    if (!ranks)
      throw std::runtime_error("cannot load rank_table_v1.bin from " +
                               options.resources_dir.string());
    const auto parsed = pb::parse_game_config_json(read_file(options.fixture));
    if (!parsed)
      throw std::runtime_error("cannot parse " + options.fixture.string());
    const auto game = pb::CompiledGame::compile(parsed.value());
    if (!game)
      throw std::runtime_error("cannot compile " + options.fixture.string());
    if (game.value().config().player_count != 3U)
      throw std::runtime_error("the fixture must have three seats");
    const auto census = census_of(game.value());

    ca::DeterministicRandom random(0x4B34'0001ULL);
    std::vector<pb::BoardContext> contexts;
    std::vector<std::array<Vector, 3>> reach(options.boards);
    for (std::size_t board = 0; board < options.boards; ++board) {
      const auto context = pb::BoardContext::build(random_board(random), ranks.value());
      if (!context)
        throw std::runtime_error("board context failed");
      contexts.push_back(context.value());
      for (auto &seat : reach[board])
        for (auto &value : seat)
          value = random.uniform_unit();
    }
    const auto groups = group_stats(contexts);

    std::vector<pb::MultiwayKernelIsa> sets;
    if (options.isa == "both") {
      sets.push_back(pb::MultiwayKernelIsa::Scalar);
      if (pb::multiway_kernel_avx2_available())
        sets.push_back(pb::MultiwayKernelIsa::Avx2);
    } else if (options.isa == "best") {
      sets.push_back(pb::best_multiway_kernel_isa());
    } else if (options.isa == "scalar") {
      sets.push_back(pb::MultiwayKernelIsa::Scalar);
    } else if (options.isa == "avx2") {
      sets.push_back(pb::MultiwayKernelIsa::Avx2);
    } else {
      throw std::runtime_error("--isa must be both, best, scalar or avx2");
    }

    // Heads-up kernels for scale.
    Vector worse{};
    Vector tied{};
    Vector better{};
    double hu_checksum = 0.0;
    const double hu_showdown_us = time_calls(contexts.size(), options.repeats, [&](const std::size_t b) {
      pb::showdown_masses(contexts[b], view(reach[b][1]), view(worse), view(tied), view(better));
      hu_checksum += worse[b % pb::live_hand_count];
    });
    const double hu_fold_us = time_calls(contexts.size(), options.repeats, [&](const std::size_t b) {
      pb::fold_mass(contexts[b], view(reach[b][1]), view(worse));
      hu_checksum += worse[b % pb::live_hand_count];
    });

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "K4 multiway kernel bench: fixture " << options.fixture.filename().string()
              << ", " << options.boards << " random boards x " << options.repeats
              << " timed repeats (single thread), best instruction set "
              << pb::multiway_kernel_isa_name(pb::best_multiway_kernel_isa()) << ", scratch "
              << pb::MultiwayScratch::bytes() << " bytes\n";
    std::cout << "rank groups: singleton share " << 100.0 * groups.singleton_share
              << " % of live hands, " << groups.groups_per_board
              << " groups per board, hand-weighted mean group size " << groups.weighted_group_size
              << ", largest group " << groups.largest_group << '\n';
    std::cout << "census per hero pass and board (seat 0/1/2): three-active "
              << census.three_active[0] << '/' << census.three_active[1] << '/'
              << census.three_active[2] << ", two-active in 3-way pots "
              << census.two_active_three_way_pot[0] << '/' << census.two_active_three_way_pot[1]
              << '/' << census.two_active_three_way_pot[2] << ", two-active in 2-way pots "
              << census.two_active_two_way_pot[0] << '/' << census.two_active_two_way_pot[1] << '/'
              << census.two_active_two_way_pot[2] << ", D3 at hero folds "
              << census.deal_at_hero_fold[0] << '/' << census.deal_at_hero_fold[1] << '/'
              << census.deal_at_hero_fold[2] << ", D3 at won folds " << census.deal_at_won_fold[0]
              << '/' << census.deal_at_won_fold[1] << '/' << census.deal_at_won_fold[2]
              << " (tree: " << census.three_active_terminals << " three-active, "
              << census.two_active_terminals << " two-active postflop showdowns, "
              << census.postflop_folds << " postflop folds)\n";
    std::cout << "heads-up kernels: showdown_masses " << hu_showdown_us << " us, fold_mass "
              << hu_fold_us << " us per call\n";
    const auto flops = pb::multiway_kernel_flops_per_hand();
    std::cout << "flops per hand (counted): three-active tied group " << flops.three_active_group
              << ", singleton " << flops.three_active_singleton << ", two-active "
              << flops.two_active << ", D3 " << flops.deal << '\n';

    double checksum = hu_checksum;
    for (const auto isa : sets) {
      const auto timing = measure(isa, contexts, reach, groups, options);
      checksum += timing.checksum;
      double per_iteration = 0.0;
      for (std::size_t seat = 0; seat < 3U; ++seat) {
        const auto two_calls = static_cast<double>(census.two_active_three_way_pot[seat] +
                                                   census.two_active_two_way_pot[seat]);
        const auto deal_calls =
            static_cast<double>(census.deal_at_hero_fold[seat] + census.deal_at_won_fold[seat]);
        per_iteration += static_cast<double>(census.three_active[seat]) * timing.three_active_us +
                         two_calls * timing.two_active_us + deal_calls * timing.deal_us;
      }
      per_iteration *= static_cast<double>(options.batch) * 1e-6;
      const double low = per_iteration / options.speedup[1] + options.traversal[0] + options.refresh[0];
      const double high = per_iteration / options.speedup[0] + options.traversal[1] + options.refresh[1];
      std::cout << "[" << pb::multiway_kernel_isa_name(isa) << "] us per call: three-active "
                << timing.three_active_us << " (" << timing.three_active_gflops
                << " GFLOP/s), two-active " << timing.two_active_us << " ("
                << timing.two_active_gflops << " GFLOP/s), D3 " << timing.deal_us << " ("
                << timing.deal_gflops << " GFLOP/s)\n";
      std::cout << "[" << pb::multiway_kernel_isa_name(isa)
                << "] predicted kernel seconds per iteration (3 hero passes x " << options.batch
                << " boards, single thread, no prune) " << per_iteration
                << "; INFERRED wall seconds per iteration with a parallel speedup of "
                << options.speedup[0] << "-" << options.speedup[1] << ", traversal "
                << options.traversal[0] << "-" << options.traversal[1] << " s and refresh "
                << options.refresh[0] << "-" << options.refresh[1] << " s: " << low << "-" << high
                << '\n';
      std::cout << "K4_JSON {\"isa\":\"" << pb::multiway_kernel_isa_name(isa)
                << "\",\"boards\":" << options.boards << ",\"repeats\":" << options.repeats
                << ",\"three_active_us\":" << timing.three_active_us
                << ",\"two_active_us\":" << timing.two_active_us
                << ",\"deal_us\":" << timing.deal_us
                << ",\"hu_showdown_us\":" << hu_showdown_us << ",\"hu_fold_us\":" << hu_fold_us
                << ",\"singleton_share\":" << groups.singleton_share
                << ",\"groups_per_board\":" << groups.groups_per_board
                << ",\"weighted_group_size\":" << groups.weighted_group_size
                << ",\"largest_group\":" << groups.largest_group
                << ",\"kernel_seconds_per_iteration_single_thread\":" << per_iteration
                << ",\"predicted_wall_seconds_per_iteration\":[" << low << "," << high << "]}\n";
    }
    std::cout << "checksum " << std::setprecision(6) << checksum << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "MULTIWAY_KERNEL_BENCH=FAIL " << error.what() << '\n';
    return 1;
  }
}
