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
#include <cmath>
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

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool close(const double left, const double right, const double tolerance) {
  return std::abs(left - right) <= tolerance * std::max(1.0, std::abs(right));
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

pb::GameConfig load_fixture(const std::string_view name) {
  const auto path =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "fixtures" / std::string(name);
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), std::string("fixture parses: ") + std::string(name));
  return parsed.value();
}

struct Resources {
  ca::RankTable ranks;
  ca::AllInTable all_in;
  std::optional<ca::BoardCatalog> catalog;
  std::optional<ca::BucketTable> flop;
  std::optional<ca::BucketTable> turn;
  std::optional<ca::BucketTable> river;
  bool loaded{false};
  bool buckets{false};
};

Resources load_resources(const std::filesystem::path &resources_dir,
                         const std::filesystem::path &buckets_dir) {
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
    std::cout << "resources not found under '" << resources_dir.string()
              << "', building the rank and all-in tables (about a minute)\n";
    auto built_ranks = ca::RankTable::build();
    require(built_ranks.has_value(), "rank table builds");
    ranks.emplace(std::move(built_ranks.value()));
    auto built_all_in = ca::AllInTable::build(ranks.value(), 8U);
    require(built_all_in.has_value(), "all-in table builds");
    all_in.emplace(std::move(built_all_in.value()));
  }
  Resources resources{std::move(ranks.value()), std::move(all_in.value()), {}, {}, {}, {}, loaded,
                      false};
  if (!buckets_dir.empty()) {
    auto flop = ca::BucketTable::load(buckets_dir / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(buckets_dir / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(buckets_dir / "river_buckets_v1.bin");
    if (flop && turn && river) {
      resources.catalog.emplace(ca::BoardCatalog::build());
      resources.flop.emplace(std::move(flop.value()));
      resources.turn.emplace(std::move(turn.value()));
      resources.river.emplace(std::move(river.value()));
      resources.buckets = true;
    } else {
      std::cout << "bucket tables not found under '" << buckets_dir.string()
                << "', bucket policy tests are skipped\n";
    }
  }
  return resources;
}

ca::BoardHistory random_board(ca::DeterministicRandom &random) {
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
  return history;
}

enum class ReachMode { Uniform, Sparse, Ones, Zeros };

std::array<double, pb::live_hand_count> random_reach(ca::DeterministicRandom &random,
                                                     const ReachMode mode) {
  std::array<double, pb::live_hand_count> reach{};
  for (auto &value : reach) {
    switch (mode) {
    case ReachMode::Uniform:
      value = random.uniform_unit();
      break;
    case ReachMode::Sparse:
      value = random.uniform_below(2U) == 0U ? 0.0 : random.uniform_unit();
      break;
    case ReachMode::Ones:
      value = 1.0;
      break;
    case ReachMode::Zeros:
      value = 0.0;
      break;
    }
  }
  return reach;
}

pb::BoardContext make_context(const Resources &resources, const ca::BoardHistory &history,
                              const bool with_buckets) {
  pb::AbstractionTables tables;
  if (with_buckets) {
    tables.catalog = &resources.catalog.value();
    tables.flop = &resources.flop.value();
    tables.turn = &resources.turn.value();
    tables.river = &resources.river.value();
  }
  const auto context = pb::BoardContext::build(history, resources.ranks,
                                               with_buckets ? &tables : nullptr);
  require(context.has_value(),
          std::string("board context builds: ") +
              (context ? "" : pb::kernel_error_name(context.error())));
  return context.value();
}

void test_board_context(const Resources &resources) {
  ca::DeterministicRandom random(0x5031'0001ULL);
  for (int board = 0; board < 20; ++board) {
    const auto history = random_board(random);
    const auto context = make_context(resources, history, false);
    require(context.combo_ids().size() == pb::live_hand_count, "465 live hands");
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      require(context.hand_index(context.combo_ids()[hand]) == hand, "hand index inverts combo ids");
      require(((context.cards()[hand][0] | context.cards()[hand][1]) & 0xC0U) == 0U &&
                  ((context.board_mask() >> context.cards()[hand][0]) & 1U) == 0U &&
                  ((context.board_mask() >> context.cards()[hand][1]) & 1U) == 0U,
              "live hands avoid the board");
      require(context.hand_classes()[hand] < 81U, "hand class is one of 81");
      require(context.row(gtosd::Street::Flop, hand) == ca::no_bucket &&
                  !context.has_buckets(gtosd::Street::Flop),
              "no bucket tables: rows are no_bucket");
      if (hand > 0U) {
        require(context.ranks()[context.order_by_rank()[hand]] >=
                    context.ranks()[context.order_by_rank()[hand - 1U]],
                "rank order is non-decreasing");
      }
    }
    std::uint32_t live_cards = 0U;
    for (std::uint8_t card = 0; card < 36U; ++card) {
      const auto hands = context.hands_with_card(card);
      if (context.card_is_live(card)) {
        ++live_cards;
        require(hands.size() == pb::hands_per_card, "a live card is in 30 live hands");
        for (const auto hand : hands) {
          require(context.cards()[hand][0] == card || context.cards()[hand][1] == card,
                  "incidence lists name hands containing the card");
        }
      } else {
        require(hands.empty(), "board cards have no live hands");
      }
    }
    require(live_cards == 31U, "31 live cards");
    require(context.distinct_rank_groups() >= 1U && context.distinct_rank_groups() <= 465U,
            "rank groups counted");
  }
  ca::BoardHistory bad = random_board(random);
  bad.river = bad.flop[0];
  require(!pb::BoardContext::build(bad, resources.ranks).has_value(), "duplicate board card rejected");
}

void test_kernels_against_reference(const Resources &resources) {
  ca::DeterministicRandom random(0x5031'0002ULL);
  std::array<double, pb::live_hand_count> fold_sweep{};
  std::array<double, pb::live_hand_count> fold_reference{};
  std::array<double, pb::live_hand_count> worse{};
  std::array<double, pb::live_hand_count> tied{};
  std::array<double, pb::live_hand_count> better{};
  std::array<double, pb::live_hand_count> worse_reference{};
  std::array<double, pb::live_hand_count> tied_reference{};
  std::array<double, pb::live_hand_count> better_reference{};
  std::uint32_t boards_with_ties = 0U;
  for (int board = 0; board < 200; ++board) {
    const auto context = make_context(resources, random_board(random), false);
    if (context.distinct_rank_groups() < pb::live_hand_count) {
      ++boards_with_ties;
    }
    for (const auto mode : {ReachMode::Uniform, ReachMode::Sparse, ReachMode::Ones}) {
      const auto reach = random_reach(random, mode);
      pb::fold_mass(context, reach, fold_sweep);
      pb::fold_mass_reference(context, reach, fold_reference);
      pb::showdown_masses(context, reach, worse, tied, better);
      pb::showdown_masses_reference(context, reach, worse_reference, tied_reference,
                                    better_reference);
      for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
        require(close(fold_sweep[hand], fold_reference[hand], 1e-12),
                "fold mass equals the pairwise reference");
        require(close(worse[hand], worse_reference[hand], 1e-12), "worse mass equals reference");
        require(close(tied[hand], tied_reference[hand], 1e-12), "tied mass equals reference");
        require(close(better[hand], better_reference[hand], 1e-12), "better mass equals reference");
        require(close(worse[hand] + tied[hand] + better[hand], fold_sweep[hand], 1e-12),
                "worse + tied + better equals the disjoint mass");
        if (mode == ReachMode::Ones) {
          require(std::abs(fold_sweep[hand] - 406.0) <= 1e-9,
                  "every hand has 406 disjoint opponents");
        }
      }
    }
  }
  const auto context = make_context(resources, random_board(random), false);
  const auto zeros = random_reach(random, ReachMode::Zeros);
  pb::showdown_masses(context, zeros, worse, tied, better);
  pb::fold_mass(context, zeros, fold_sweep);
  for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
    require(worse[hand] == 0.0 && tied[hand] == 0.0 && better[hand] == 0.0 &&
                fold_sweep[hand] == 0.0,
            "zero reach gives zero masses");
  }
  require(boards_with_ties == 200U, "every board has hands of equal rank");
  std::cout << "kernels: 200 boards x 3 reach patterns match the pairwise reference\n";
}

void test_all_in_kernel(const Resources &resources) {
  ca::DeterministicRandom random(0x5031'0003ULL);
  std::array<double, pb::live_hand_count> win{};
  std::array<double, pb::live_hand_count> tie{};
  std::array<double, pb::live_hand_count> lose{};
  std::array<double, pb::live_hand_count> win_reference{};
  std::array<double, pb::live_hand_count> tie_reference{};
  std::array<double, pb::live_hand_count> lose_reference{};
  std::array<double, pb::live_hand_count> disjoint{};
  double build_seconds = 0.0;
  for (int board = 0; board < 20; ++board) {
    const auto context = make_context(resources, random_board(random), false);
    const auto started = Clock::now();
    const auto cache = pb::AllInEquityCache::build(context, resources.all_in);
    build_seconds += std::chrono::duration<double>(Clock::now() - started).count();
    require(cache.has_value(), "all-in equity cache builds");
    for (const auto mode : {ReachMode::Uniform, ReachMode::Sparse}) {
      const auto reach = random_reach(random, mode);
      cache.value().masses(context, reach, win, tie, lose);
      cache.value().masses_reference(context, resources.all_in, reach, win_reference,
                                     tie_reference, lose_reference);
      pb::fold_mass(context, reach, disjoint);
      for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
        require(close(win[hand], win_reference[hand], 1e-12), "all-in win mass equals reference");
        require(close(tie[hand], tie_reference[hand], 1e-12), "all-in tie mass equals reference");
        require(close(lose[hand], lose_reference[hand], 1e-12),
                "all-in lose mass equals reference");
        require(close(win[hand] + tie[hand] + lose[hand], disjoint[hand], 1e-12),
                "all-in masses add up to the disjoint mass");
      }
    }
  }
  std::cout << "all-in cache: 20 boards match the pairwise reference; build "
            << build_seconds / 20.0 * 1000.0 << " ms per board\n";
}

// Value of the pair (hero hand h, opponent hand o) under the policy, in antes.
class PairReference {
public:
  PairReference(const pb::CompiledGame &game, const pb::BoardContext &context,
                const pb::Policy &policy, const ca::AllInTable &all_in, const std::uint8_t hero)
      : game_(game), context_(context), policy_(policy), all_in_(all_in), hero_(hero) {}

  double value(const std::uint32_t node_id, const std::uint16_t hero_hand,
               const std::uint16_t opponent_hand) const {
    const auto &node = game_.nodes()[node_id];
    constexpr double scale = 1.0 / gtosd::Money::units_per_ante;
    switch (node.kind) {
    case pb::NodeKind::TerminalFold:
      return static_cast<double>(game_.fold_payoffs(node_id)[hero_]) * scale;
    case pb::NodeKind::TerminalShowdown: {
      const auto hero_bit = static_cast<std::uint8_t>(1U << hero_);
      const double win = static_cast<double>(game_.showdown_payoffs(node_id, hero_bit)[hero_]) * scale;
      const double tie =
          static_cast<double>(game_.showdown_payoffs(node_id, node.active_mask)[hero_]) * scale;
      const double lose =
          static_cast<double>(
              game_.showdown_payoffs(node_id, static_cast<std::uint8_t>(node.active_mask & ~hero_bit))[hero_]) *
          scale;
      if (node.street == gtosd::Street::Preflop) {
        const auto outcome = all_in_.outcome(context_.combo_ids()[hero_hand],
                                             context_.combo_ids()[opponent_hand]);
        const auto total = static_cast<double>(outcome.total());
        return (win * outcome.wins + tie * outcome.ties + lose * outcome.losses) / total;
      }
      const auto hero_rank = context_.ranks()[hero_hand];
      const auto opponent_rank = context_.ranks()[opponent_hand];
      return hero_rank > opponent_rank ? win : hero_rank < opponent_rank ? lose : tie;
    }
    case pb::NodeKind::Chance:
      return value(game_.edges_of(node_id)[0].child, hero_hand, opponent_hand);
    case pb::NodeKind::Decision:
      break;
    }
    const auto acting_hand = node.actor == hero_ ? hero_hand : opponent_hand;
    const double *probabilities = policy_.probabilities(node_id, acting_hand, context_);
    double total = 0.0;
    const auto edges = game_.edges_of(node_id);
    for (std::size_t action = 0; action < edges.size(); ++action) {
      if (probabilities[action] != 0.0) {
        total += probabilities[action] * value(edges[action].child, hero_hand, opponent_hand);
      }
    }
    return total;
  }

private:
  const pb::CompiledGame &game_;
  const pb::BoardContext &context_;
  const pb::Policy &policy_;
  const ca::AllInTable &all_in_;
  std::uint8_t hero_;
};

bool disjoint_hands(const std::array<std::uint8_t, 2> &left,
                    const std::array<std::uint8_t, 2> &right) {
  return left[0] != right[0] && left[0] != right[1] && left[1] != right[0] &&
         left[1] != right[1];
}

void randomize_hand_policy(pb::HandPolicy &policy, const pb::CompiledGame &game,
                           ca::DeterministicRandom &random) {
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      auto row = policy.row(node.id, hand);
      double total = 0.0;
      for (auto &probability : row) {
        probability = random.uniform_below(4U) == 0U ? 0.0 : random.uniform_unit();
        total += probability;
      }
      if (total == 0.0) {
        row[0] = 1.0;
        total = 1.0;
      }
      for (auto &probability : row) {
        probability /= total;
      }
    }
  }
}

void test_traversal_against_pairwise(const Resources &resources, const std::string_view fixture,
                                     const int boards, const bool both_heroes) {
  const auto game = pb::CompiledGame::compile(load_fixture(fixture));
  require(game.has_value(), "game compiles");
  ca::DeterministicRandom random(0x5031'0004ULL);
  const pb::HeadsUpShowdownKernel kernel;
  for (int board = 0; board < boards; ++board) {
    const auto context = make_context(resources, random_board(random), false);
    const auto cache = pb::AllInEquityCache::build(context, resources.all_in);
    require(cache.has_value(), "all-in cache builds");
    pb::HandPolicy policy(game.value());
    randomize_hand_policy(policy, game.value(), random);
    const auto reach = random_reach(random, ReachMode::Sparse);
    pb::ValueTraversal traversal(game.value(), context, kernel, &cache.value());
    std::array<double, pb::live_hand_count> values{};
    std::array<double, pb::live_hand_count> repeated{};
    std::array<double, pb::live_hand_count> best_response{};
    for (std::uint8_t hero = 0; hero < (both_heroes ? 2U : 1U); ++hero) {
      const auto started = Clock::now();
      require(traversal.evaluate(policy, hero, reach, values).has_value(), "traversal evaluates");
      const auto traversal_seconds = std::chrono::duration<double>(Clock::now() - started).count();
      const auto counters = traversal.counters();
      require(traversal.evaluate(policy, hero, reach, repeated).has_value(), "second evaluation");
      require(values == repeated, "the traversal is deterministic");
      pb::TraversalOptions options;
      options.best_response = true;
      require(traversal.evaluate(policy, hero, reach, best_response, options).has_value(),
              "best-response traversal evaluates");

      const PairReference reference(game.value(), context, policy, resources.all_in, hero);
      const auto reference_started = Clock::now();
      double maximum_error = 0.0;
      for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
        double expected = 0.0;
        for (std::uint16_t other = 0; other < pb::live_hand_count; ++other) {
          if (other != hand && reach[other] != 0.0 &&
              disjoint_hands(context.cards()[hand], context.cards()[other])) {
            expected += reach[other] * reference.value(game.value().root(), hand, other);
          }
        }
        maximum_error = std::max(maximum_error, std::abs(values[hand] - expected));
        require(close(values[hand], expected, 1e-9),
                "traversal value equals the pairwise recursion");
        require(best_response[hand] >= values[hand] - 1e-9,
                "best response dominates the policy value");
      }
      const auto reference_seconds =
          std::chrono::duration<double>(Clock::now() - reference_started).count();
      std::cout << fixture << " board " << board << " hero " << static_cast<unsigned>(hero)
                << ": traversal " << traversal_seconds * 1000.0 << " ms, nodes "
                << counters.nodes_visited << ", pruned " << counters.subtrees_pruned
                << ", terminals " << counters.terminals_evaluated << ", pairwise reference "
                << reference_seconds << " s, max error " << maximum_error << '\n';
    }
  }
}

void test_bucket_policy(const Resources &resources) {
  if (!resources.buckets) {
    std::cout << "bucket policy: SKIPPED (no bucket tables)\n";
    return;
  }
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_co40_v1.json"));
  require(game.has_value(), "CO40 compiles");
  const auto layout = pb::layout_state(game.value(), resources.flop->capacity(),
                                       resources.turn->capacity(), resources.river->capacity());
  ca::DeterministicRandom random(0x5031'0005ULL);
  const pb::HeadsUpShowdownKernel kernel;
  const auto context = make_context(resources, random_board(random), true);
  for (const auto street : {gtosd::Street::Flop, gtosd::Street::Turn, gtosd::Street::River}) {
    require(context.has_buckets(street), "context carries the street buckets");
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      require(context.row(street, hand) < pb::StateLayout::rows_for(street, layout.flop_capacity,
                                                                    layout.turn_capacity,
                                                                    layout.river_capacity),
              "bucket rows are within the capacity");
    }
  }
  const auto cache = pb::AllInEquityCache::build(context, resources.all_in);
  require(cache.has_value(), "all-in cache builds");

  pb::BucketPolicy bucket_policy(game.value(), layout);
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row_index = 0; row_index < rows; ++row_index) {
      auto row = bucket_policy.row(node.id, row_index);
      double total = 0.0;
      for (auto &probability : row) {
        probability = 0.05 + random.uniform_unit();
        total += probability;
      }
      for (auto &probability : row) {
        probability /= total;
      }
    }
  }
  // The same strategy expressed per hand must give the same values.
  pb::HandPolicy hand_policy(game.value());
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision) {
      continue;
    }
    for (std::uint16_t hand = 0; hand < pb::live_hand_count; ++hand) {
      const auto source = bucket_policy.row(node.id, context.row(node.street, hand));
      auto target = hand_policy.row(node.id, hand);
      std::copy(source.begin(), source.end(), target.begin());
    }
  }
  pb::ValueTraversal traversal(game.value(), context, kernel, &cache.value());
  const auto reach = random_reach(random, ReachMode::Uniform);
  std::array<double, pb::live_hand_count> from_buckets{};
  std::array<double, pb::live_hand_count> from_hands{};
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    const auto started = Clock::now();
    require(traversal.evaluate(bucket_policy, hero, reach, from_buckets).has_value(),
            "bucket policy traversal evaluates");
    const auto seconds = std::chrono::duration<double>(Clock::now() - started).count();
    require(traversal.evaluate(hand_policy, hero, reach, from_hands).has_value(),
            "hand policy traversal evaluates");
    for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
      require(close(from_buckets[hand], from_hands[hand], 1e-12),
              "bucket rows and per-hand rows give the same values");
    }
    std::cout << "CO40 bucket policy hero " << static_cast<unsigned>(hero) << ": traversal "
              << seconds * 1000.0 << " ms, nodes " << traversal.counters().nodes_visited << '\n';
  }
}

void test_traversal_rejections(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  ca::DeterministicRandom random(0x5031'0006ULL);
  const auto context = make_context(resources, random_board(random), false);
  const pb::HeadsUpShowdownKernel kernel;
  pb::HandPolicy policy(game.value());
  const auto reach = random_reach(random, ReachMode::Ones);
  std::array<double, pb::live_hand_count> values{};
  pb::ValueTraversal without_all_in(game.value(), context, kernel, nullptr);
  const auto missing = without_all_in.evaluate(policy, 0U, reach, values);
  require(!missing.has_value() && missing.error() == pb::KernelError::MissingTable,
          "a reachable preflop all-in without the exact table fails the traversal");
  const auto cache = pb::AllInEquityCache::build(context, resources.all_in);
  pb::ValueTraversal traversal(game.value(), context, kernel, &cache.value());
  require(!traversal.evaluate(policy, 2U, reach, values).has_value(), "hero index is validated");
  const auto layout = pb::layout_state(game.value(), 8U, 8U, 8U);
  pb::BucketPolicy bucket_policy(game.value(), layout);
  const auto no_buckets = traversal.evaluate(bucket_policy, 0U, reach, values);
  require(!no_buckets.has_value() && no_buckets.error() == pb::KernelError::MissingTable,
          "a bucket policy without bucket rows in the context fails the traversal");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      if (name == "--resources-dir") {
        resources_dir = argv[index + 1];
      } else if (name == "--buckets-dir") {
        buckets_dir = argv[index + 1];
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    const auto resources = load_resources(resources_dir, buckets_dir);
    std::cout << "resources " << (resources.loaded ? "loaded" : "built") << ", bucket tables "
              << (resources.buckets ? "loaded" : "absent") << '\n';
    test_board_context(resources);
    test_kernels_against_reference(resources);
    test_all_in_kernel(resources);
    test_traversal_against_pairwise(resources, "preflop_blueprint_hu10_reduced_v1.json", 2, true);
    test_traversal_against_pairwise(resources, "preflop_blueprint_hu10_full_v1.json", 1, false);
    test_bucket_policy(resources);
    test_traversal_rejections(resources);
    std::cout << "PREFLOP_BLUEPRINT_KERNEL_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_KERNEL_TESTS=FAIL " << error.what() << " after " << assertions
              << " assertions\n";
    return 1;
  }
}
