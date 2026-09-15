#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"
#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/finite_game.hpp"
#include "gtosd/solver/solver.hpp"

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

gtosd::CardId card(const std::string_view text) {
  const auto parsed = gtosd::parse_card(text);
  require(parsed.has_value(), "card parses");
  return parsed.value();
}

struct Resources {
  std::optional<ca::RankTable> ranks;
  std::optional<ca::AllInTable> all_in;
  std::optional<ca::BoardCatalog> catalog;
  std::optional<ca::BucketTable> flop;
  std::optional<ca::BucketTable> turn;
  std::optional<ca::BucketTable> river;
  bool loaded{false};
  bool buckets_loaded{false};

  [[nodiscard]] pb::TrainerResources view() const {
    pb::TrainerResources view;
    view.ranks = &ranks.value();
    view.all_in = &all_in.value();
    view.catalog = &catalog.value();
    view.flop = &flop.value();
    view.turn = &turn.value();
    view.river = &river.value();
    return view;
  }
  [[nodiscard]] pb::TrainerConfig config() const {
    pb::TrainerConfig config;
    config.flop_capacity = flop->capacity();
    config.turn_capacity = turn->capacity();
    config.river_capacity = river->capacity();
    return config;
  }
};

ca::OpponentGroups synthetic_groups() {
  std::array<std::uint8_t, 81> ranking{};
  std::array<double, 81> equity{};
  for (std::uint8_t index = 0U; index < ranking.size(); ++index) {
    ranking[index] = index;
    equity[index] = 1.0 - static_cast<double>(index) / 81.0;
  }
  return ca::OpponentGroups::from_ranking(ranking, equity, "synthetic_trainer_test_groups");
}

Resources load_resources(const std::filesystem::path &resources_dir,
                         const std::filesystem::path &buckets_dir) {
  Resources resources;
  if (!resources_dir.empty()) {
    auto ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
    auto all_in = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
    if (ranks && all_in) {
      resources.ranks.emplace(std::move(ranks.value()));
      resources.all_in.emplace(std::move(all_in.value()));
      resources.loaded = true;
    }
  }
  if (!resources.loaded) {
    std::cout << "resources not found, building the rank and all-in tables (about a minute)\n";
    auto ranks = ca::RankTable::build();
    require(ranks.has_value(), "rank table builds");
    resources.ranks.emplace(std::move(ranks.value()));
    auto all_in = ca::AllInTable::build(resources.ranks.value(), 8U);
    require(all_in.has_value(), "all-in table builds");
    resources.all_in.emplace(std::move(all_in.value()));
  }
  resources.catalog.emplace(ca::BoardCatalog::build());
  if (!buckets_dir.empty()) {
    auto flop = ca::BucketTable::load(buckets_dir / "flop_buckets_v1.bin");
    auto turn = ca::BucketTable::load(buckets_dir / "turn_buckets_v1.bin");
    auto river = ca::BucketTable::load(buckets_dir / "river_buckets_v1.bin");
    if (flop && turn && river) {
      resources.flop.emplace(std::move(flop.value()));
      resources.turn.emplace(std::move(turn.value()));
      resources.river.emplace(std::move(river.value()));
      resources.buckets_loaded = true;
    }
  }
  if (!resources.buckets_loaded) {
    std::cout << "bucket tables not found, clustering small tables from synthetic groups\n";
    const auto groups = synthetic_groups();
    auto flop_features =
        ca::FlopFeatureTable::build(resources.catalog.value(), resources.ranks.value(), 8U);
    auto turn_features =
        ca::TurnFeatureTable::build(resources.catalog.value(), resources.ranks.value(), 8U);
    auto river_features = ca::RiverFeatureTable::build(resources.catalog.value(),
                                                       resources.ranks.value(), groups, 8U);
    require(flop_features.has_value() && turn_features.has_value() && river_features.has_value(),
            "feature tables build");
    ca::ClusteringParameters parameters;
    parameters.restarts = 2U;
    parameters.screening_iterations = 3U;
    parameters.maximum_iterations = 4U;
    parameters.screening_sample = 50'000U;
    parameters.threads = 8U;
    parameters.capacity = 8U;
    auto flop = ca::BucketTable::build_flop(resources.catalog.value(), flop_features.value(), parameters);
    parameters.capacity = 16U;
    auto turn = ca::BucketTable::build_turn(resources.catalog.value(), turn_features.value(), parameters);
    parameters.capacity = 32U;
    auto river =
        ca::BucketTable::build_river(resources.catalog.value(), river_features.value(), parameters);
    require(flop.has_value() && turn.has_value() && river.has_value(), "bucket tables build");
    resources.flop.emplace(std::move(flop.value()));
    resources.turn.emplace(std::move(turn.value()));
    resources.river.emplace(std::move(river.value()));
  }
  return resources;
}

ca::BoardHistory make_history(const std::array<std::string_view, 5> &texts) {
  std::array<gtosd::CardId, 3> flop{card(texts[0]), card(texts[1]), card(texts[2])};
  std::sort(flop.begin(), flop.end());
  ca::BoardHistory history;
  history.flop = flop;
  history.turn = card(texts[3]);
  history.river = card(texts[4]);
  return history;
}

// Boards on ranks six to nine plus an ace; hand subsets on tens and jacks
// (player 0) and queens and kings (player 1): every pair is disjoint from
// every board and from each other, so the deal probability is uniform.
pb::TrainingBoards oracle_boards() {
  pb::TrainingBoards boards;
  boards.histories = {make_history({"6s", "7d", "8c", "9h", "As"}),
                      make_history({"6d", "6h", "9s", "7c", "Ad"}),
                      make_history({"7s", "8h", "9c", "6c", "Ah"})};
  boards.weights = {1.0, 2.0, 3.0};
  boards.sample = false;
  return boards;
}

std::vector<std::uint16_t> combos_from_cards(const std::array<std::string_view, 4> &texts) {
  std::vector<std::uint16_t> combos;
  for (std::size_t first = 0; first < texts.size(); ++first) {
    for (std::size_t second = first + 1U; second < texts.size(); ++second) {
      combos.push_back(ca::combo_index(card(texts[first]), card(texts[second])));
    }
  }
  return combos;
}

pb::HandSubsets oracle_subsets() {
  pb::HandSubsets subsets;
  subsets.combos[0] = combos_from_cards({"Ts", "Th", "Js", "Jh"});
  subsets.combos[1] = combos_from_cards({"Qs", "Qh", "Ks", "Kh"});
  return subsets;
}

// Builds the finite game of the reduced problem: chance over boards, chance
// over disjoint deals, then a copy of the compiled tree per deal whose
// information sets are keyed by (player, compiled node, bucket row).
class FiniteGameBuilder {
public:
  FiniteGameBuilder(const pb::CompiledGame &game, const Resources &resources)
      : game_(game), resources_(resources) {}

  gtosd::FiniteGame build(const pb::TrainingBoards &boards, const pb::HandSubsets &subsets) {
    gtosd::FiniteGame finite;
    finite.game_id = "preflop_blueprint_oracle_reduced_game";
    finite.initial_pot = 3.0;
    gtosd::GameNode root;
    root.kind = gtosd::GameNodeKind::Chance;
    const auto root_id = add(std::move(root));
    double total_weight = 0.0;
    for (const auto weight : boards.weights) {
      total_weight += weight;
    }
    pb::AbstractionTables tables;
    tables.catalog = &resources_.catalog.value();
    tables.flop = &resources_.flop.value();
    tables.turn = &resources_.turn.value();
    tables.river = &resources_.river.value();
    for (std::size_t board = 0; board < boards.histories.size(); ++board) {
      const auto context =
          pb::BoardContext::build(boards.histories[board], resources_.ranks.value(), &tables);
      require(context.has_value(), "oracle board context builds");
      std::vector<std::pair<std::uint16_t, std::uint16_t>> deals;
      for (const auto hero_combo : subsets.combos[0]) {
        for (const auto opponent_combo : subsets.combos[1]) {
          const auto hero = context.value().hand_index(hero_combo);
          const auto opponent = context.value().hand_index(opponent_combo);
          require(hero != pb::no_hand && opponent != pb::no_hand, "subset hands are live");
          const auto &left = context.value().cards()[hero];
          const auto &right = context.value().cards()[opponent];
          require(left[0] != right[0] && left[0] != right[1] && left[1] != right[0] &&
                      left[1] != right[1],
                  "subset pairs are disjoint");
          deals.emplace_back(hero, opponent);
        }
      }
      gtosd::GameNode board_node;
      board_node.kind = gtosd::GameNodeKind::Chance;
      const auto board_id = add(std::move(board_node));
      nodes_[root_id].edges.push_back(
          {{static_cast<gtosd::GameActionId>(board), "board" + std::to_string(board)}, board_id,
           boards.weights[board] / total_weight});
      for (std::size_t deal = 0; deal < deals.size(); ++deal) {
        const auto child = subtree(game_.root(), context.value(), deals[deal].first,
                                   deals[deal].second);
        nodes_[board_id].edges.push_back(
            {{static_cast<gtosd::GameActionId>(deal), "deal" + std::to_string(deal)}, child,
             1.0 / static_cast<double>(deals.size())});
      }
    }
    finite.root = root_id;
    finite.nodes = std::move(nodes_);
    return finite;
  }

  static std::string information_set(const std::uint8_t player, const std::uint32_t node,
                                     const std::uint32_t row) {
    return "p" + std::to_string(player) + "|n" + std::to_string(node) + "|r" +
           std::to_string(row);
  }

private:
  gtosd::GameNodeId add(gtosd::GameNode node) {
    nodes_.push_back(std::move(node));
    return static_cast<gtosd::GameNodeId>(nodes_.size() - 1U);
  }

  gtosd::GameNodeId subtree(const std::uint32_t node_id, const pb::BoardContext &context,
                            const std::uint16_t hero_hand, const std::uint16_t opponent_hand) {
    const auto &node = game_.nodes()[node_id];
    constexpr double scale = 1.0 / gtosd::Money::units_per_ante;
    gtosd::GameNode finite;
    switch (node.kind) {
    case pb::NodeKind::TerminalFold: {
      finite.kind = gtosd::GameNodeKind::Terminal;
      const auto payoffs = game_.fold_payoffs(node_id);
      finite.payoff = {payoffs[0] * scale, payoffs[1] * scale};
      return add(std::move(finite));
    }
    case pb::NodeKind::TerminalShowdown: {
      finite.kind = gtosd::GameNodeKind::Terminal;
      const auto hero_wins = game_.showdown_payoffs(node_id, 0b01U);
      const auto tie = game_.showdown_payoffs(node_id, 0b11U);
      const auto opponent_wins = game_.showdown_payoffs(node_id, 0b10U);
      if (node.street == gtosd::Street::Preflop) {
        const auto outcome = resources_.all_in->outcome(context.combo_ids()[hero_hand],
                                                        context.combo_ids()[opponent_hand]);
        const auto total = static_cast<double>(outcome.total());
        for (std::uint8_t player = 0; player < 2U; ++player) {
          finite.payoff[player] = (outcome.wins * hero_wins[player] + outcome.ties * tie[player] +
                                   outcome.losses * opponent_wins[player]) *
                                  scale / total;
        }
      } else {
        const auto hero_rank = context.ranks()[hero_hand];
        const auto opponent_rank = context.ranks()[opponent_hand];
        const auto &row = hero_rank > opponent_rank   ? hero_wins
                          : hero_rank < opponent_rank ? opponent_wins
                                                      : tie;
        finite.payoff = {row[0] * scale, row[1] * scale};
      }
      return add(std::move(finite));
    }
    case pb::NodeKind::Chance: {
      finite.kind = gtosd::GameNodeKind::Chance;
      const auto id = add(std::move(finite));
      const auto child =
          subtree(game_.edges_of(node_id)[0].child, context, hero_hand, opponent_hand);
      nodes_[id].edges.push_back({{0U, "street"}, child, 1.0});
      return id;
    }
    case pb::NodeKind::Decision:
      break;
    }
    finite.kind = gtosd::GameNodeKind::Decision;
    finite.player = node.actor;
    const auto acting_hand = node.actor == 0U ? hero_hand : opponent_hand;
    finite.information_set =
        information_set(node.actor, node_id, context.row(node.street, acting_hand));
    const auto id = add(std::move(finite));
    const auto edges = game_.edges_of(node_id);
    for (std::size_t action = 0; action < edges.size(); ++action) {
      const auto child = subtree(edges[action].child, context, hero_hand, opponent_hand);
      nodes_[id].edges.push_back(
          {{static_cast<gtosd::GameActionId>(action),
            std::to_string(static_cast<unsigned>(edges[action].action.type)) + ":" +
                std::to_string(edges[action].action.amount.units())},
           child, 0.0});
    }
    return id;
  }

  const pb::CompiledGame &game_;
  const Resources &resources_;
  std::vector<gtosd::GameNode> nodes_;
};

struct ParsedKey {
  std::uint8_t player;
  std::uint32_t node;
  std::uint32_t row;
};

ParsedKey parse_key(const std::string &key) {
  const auto node_position = key.find("|n");
  const auto row_position = key.find("|r");
  require(key.size() > 2U && key[0] == 'p' && node_position != std::string::npos &&
              row_position != std::string::npos,
          "information set key parses");
  ParsedKey parsed;
  parsed.player = static_cast<std::uint8_t>(std::stoul(key.substr(1, node_position - 1)));
  parsed.node = static_cast<std::uint32_t>(
      std::stoul(key.substr(node_position + 2, row_position - node_position - 2)));
  parsed.row = static_cast<std::uint32_t>(std::stoul(key.substr(row_position + 2)));
  return parsed;
}

void test_finite_game_oracle(const Resources &resources) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto boards = oracle_boards();
  const auto subsets = oracle_subsets();
  constexpr std::uint64_t iterations = 25U;

  FiniteGameBuilder builder(game.value(), resources);
  const auto finite = builder.build(boards, subsets);
  const auto summary = gtosd::validate_finite_game(finite);
  require(summary.has_value(),
          std::string("finite game validates: ") +
              (summary ? "" : gtosd::solver_error_name(summary.error())));
  gtosd::SolverConfig solver_config;
  solver_config.algorithm = gtosd::SolverAlgorithm::LinearCfr;
  solver_config.iterations = iterations;
  solver_config.thread_count = 1U;
  const auto solved = gtosd::solve_finite_game(finite, solver_config);
  require(solved.has_value(), "finite game solves");

  auto config = resources.config();
  config.threads = 2U;
  config.scheme = pb::WeightingScheme::Linear;
  config.update_mode = pb::UpdateMode::Simultaneous;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config, &boards, &subsets);
  require(trainer.has_value(),
          std::string("trainer creates: ") +
              (trainer ? "" : pb::trainer_error_name(trainer.error())));
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    require(trainer.value()->iterate().has_value(), "exact-mode iteration succeeds");
  }
  const auto &layout = trainer.value()->layout();
  const auto &regrets = trainer.value()->regrets();
  const auto &sums = trainer.value()->strategy_sums();
  const auto average = trainer.value()->average_policy();
  std::vector<std::uint8_t> covered(regrets.size(), 0U);
  double maximum_regret_error = 0.0;
  double maximum_strategy_error = 0.0;
  std::uint64_t compared = 0U;
  for (const auto &[key, buffer] : solved.value().checkpoint.information_sets) {
    const auto parsed = parse_key(key);
    const auto &node = game.value().nodes()[parsed.node];
    require(node.kind == pb::NodeKind::Decision && node.actor == parsed.player &&
                buffer.actions.size() == node.action_count,
            "oracle information set maps onto a compiled decision");
    const auto offset = layout.offsets[parsed.node] +
                        static_cast<std::uint64_t>(parsed.row) * node.action_count;
    const auto average_row = average.row(parsed.node, parsed.row);
    const auto &oracle_average = solved.value().average_strategy.at(key);
    for (std::size_t action = 0; action < node.action_count; ++action) {
      covered[offset + action] = 1U;
      maximum_regret_error = std::max(
          maximum_regret_error, std::abs(regrets[offset + action] - buffer.cumulative_regret[action]));
      maximum_strategy_error =
          std::max(maximum_strategy_error,
                   std::abs(sums[offset + action] - buffer.cumulative_strategy[action]));
      require(close(regrets[offset + action], buffer.cumulative_regret[action], 1e-9),
              "cumulative regret equals the FiniteGame oracle within 1e-9");
      require(close(sums[offset + action], buffer.cumulative_strategy[action], 1e-9),
              "cumulative strategy equals the FiniteGame oracle within 1e-9");
      require(close(average_row[action], oracle_average.probabilities[action], 1e-9),
              "average strategy equals the FiniteGame oracle within 1e-9");
      ++compared;
    }
  }
  for (std::size_t cell = 0; cell < regrets.size(); ++cell) {
    if (covered[cell] == 0U) {
      require(regrets[cell] == 0.0 && sums[cell] == 0.0,
              "cells outside the reduced game stay untouched");
    }
  }
  const auto exact = trainer.value()->estimate_exploitability(0U);
  require(exact.has_value() && exact.value().exact, "exact evaluation on the listed boards");
  const auto nash_conv =
      gtosd::calculate_nash_conv(finite, solved.value().average_strategy);
  require(nash_conv.has_value(), "FiniteGame NashConv computes");
  // The profile values coincide. The trainer's best response is the physical
  // one (per hand on every board) and therefore dominates the FiniteGame
  // best response, which is constrained to one action per bucket.
  require(close(exact.value().ev[0], nash_conv.value().profile_value[0], 1e-9) &&
              close(exact.value().ev[1], nash_conv.value().profile_value[1], 1e-9),
          "exact profile values equal calculate_nash_conv within 1e-9");
  require(exact.value().best_response[0] >= nash_conv.value().best_response_value[0] - 1e-9 &&
              exact.value().best_response[1] >= nash_conv.value().best_response_value[1] - 1e-9 &&
              exact.value().nashconv >= nash_conv.value().nash_conv - 1e-9,
          "physical best response dominates the abstract-game best response");
  std::cout << "oracle: finite game " << summary.value().nodes << " nodes, "
            << summary.value().information_sets << " information sets, " << compared
            << " cells compared, max regret error " << maximum_regret_error
            << ", max strategy error " << maximum_strategy_error << ", nashconv "
            << exact.value().nashconv << " (oracle " << nash_conv.value().nash_conv << "), EV ["
            << exact.value().ev[0] << ", " << exact.value().ev[1] << "], "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

void test_determinism(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  std::string reference;
  std::vector<double> reference_regrets;
  std::vector<double> reference_sums;
  for (const unsigned threads : {1U, 2U, 4U, 8U}) {
    auto config = resources.config();
    config.threads = threads;
    config.batch_boards = 8U;
    auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
    require(trainer.has_value(), "trainer creates");
    for (int iteration = 0; iteration < 4; ++iteration) {
      require(trainer.value()->iterate().has_value(), "iteration succeeds");
    }
    const auto fingerprint = trainer.value()->state_fingerprint();
    if (reference.empty()) {
      reference = fingerprint;
      reference_regrets = trainer.value()->regrets();
      reference_sums = trainer.value()->strategy_sums();
    }
    require(trainer.value()->regrets() == reference_regrets &&
                trainer.value()->strategy_sums() == reference_sums,
            "tables are bit-identical across thread counts");
    require(fingerprint == reference, "state is bit-identical across thread counts");
  }
  {
    auto plain_config = resources.config();
    plain_config.threads = 1U;
    plain_config.batch_boards = 8U;
    plain_config.partition_target_nodes = 1'000'000U;
    auto plain = pb::Trainer::create(game.value(), resources.view(), plain_config);
    require(plain.has_value(), "single-unit trainer creates");
    for (int iteration = 0; iteration < 4; ++iteration) {
      require(plain.value()->iterate().has_value(), "iteration succeeds");
    }
    std::size_t plain_differing = 0U;
    for (std::size_t cell = 0; cell < reference_regrets.size(); ++cell) {
      if (plain.value()->regrets()[cell] != reference_regrets[cell] ||
          plain.value()->strategy_sums()[cell] != reference_sums[cell]) {
        ++plain_differing;
      }
    }
    std::cout << "single-unit partition (" << plain.value()->partition().unit_roots.size()
              << " units, " << plain.value()->partition().top_nodes << " top nodes) vs default: "
              << plain_differing << " cells differ\n";
  }
  auto config = resources.config();
  config.threads = 3U;
  config.batch_boards = 8U;
  config.partition_target_nodes = 24U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
  require(trainer.has_value(), "trainer with a fine partition creates");
  require(trainer.value()->partition().unit_roots.size() > 8U, "fine partition has many units");
  std::cout << "fine partition: " << trainer.value()->partition().unit_roots.size() << " units, "
            << trainer.value()->partition().top_nodes << " top nodes, largest unit "
            << trainer.value()->partition().largest_unit_nodes << '\n';
  for (int iteration = 0; iteration < 4; ++iteration) {
    require(trainer.value()->iterate().has_value(), "iteration succeeds");
  }
  std::size_t differing = 0U;
  double maximum_difference = 0.0;
  for (std::size_t cell = 0; cell < reference_regrets.size(); ++cell) {
    const auto regret = trainer.value()->regrets()[cell];
    const auto sum = trainer.value()->strategy_sums()[cell];
    if (regret != reference_regrets[cell] || sum != reference_sums[cell]) {
      if (differing < 6U) {
        std::cout << "partition mismatch cell " << cell << ": regret " << regret << " vs "
                  << reference_regrets[cell] << ", sum " << sum << " vs " << reference_sums[cell]
                  << '\n';
      }
      ++differing;
      maximum_difference = std::max({maximum_difference, std::abs(regret - reference_regrets[cell]),
                                     std::abs(sum - reference_sums[cell])});
    }
  }
  if (differing != 0U) {
    std::cout << "partition mismatch: " << differing << " cells differ, max difference "
              << maximum_difference << '\n';
  }
  require(differing == 0U && trainer.value()->state_fingerprint() == reference,
          "state does not depend on the subtree partition");
  std::cout << "determinism: fingerprint " << reference << " for 1/2/4/8 threads and a "
            << trainer.value()->partition().unit_roots.size() << "-unit partition\n";
}

void test_resume(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  auto config = resources.config();
  config.threads = 4U;
  config.batch_boards = 8U;
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  auto continuous = pb::Trainer::create(game.value(), resources.view(), config);
  require(continuous.has_value(), "continuous trainer creates");
  for (int iteration = 0; iteration < 6; ++iteration) {
    require(continuous.value()->iterate().has_value(), "iteration succeeds");
  }

  auto first = pb::Trainer::create(game.value(), resources.view(), config);
  require(first.has_value(), "first trainer creates");
  for (int iteration = 0; iteration < 3; ++iteration) {
    require(first.value()->iterate().has_value(), "iteration succeeds");
  }
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "trainer_checkpoint.bin";
  require(first.value()->save_checkpoint(path).has_value(), "checkpoint saves");
  require(!std::filesystem::exists(path.string() + ".tmp"), "temporary file renamed away");

  auto resumed = pb::Trainer::create(game.value(), resources.view(), config);
  require(resumed.has_value(), "resumed trainer creates");
  require(resumed.value()->load_checkpoint(path).has_value(), "checkpoint loads");
  require(resumed.value()->iteration() == 3U, "iteration restored");
  for (int iteration = 0; iteration < 3; ++iteration) {
    require(resumed.value()->iterate().has_value(), "iteration succeeds");
  }
  require(resumed.value()->regrets() == continuous.value()->regrets() &&
              resumed.value()->strategy_sums() == continuous.value()->strategy_sums(),
          "resume from checkpoint equals the continuous run bit for bit");
  require(resumed.value()->state_fingerprint() == continuous.value()->state_fingerprint(),
          "iteration, RNG states and tables coincide after the resume");
  const auto continuous_estimate = continuous.value()->estimate_exploitability(50U);
  const auto resumed_estimate = resumed.value()->estimate_exploitability(50U);
  require(continuous_estimate.has_value() && resumed_estimate.has_value() &&
              resumed_estimate.value().max_gain == continuous_estimate.value().max_gain,
          "evaluation RNG restored: identical sampled estimate");

  // Corruption and identity mismatch are rejected.
  {
    auto bytes = read_file(path);
    bytes[bytes.size() / 2U] = static_cast<char>(bytes[bytes.size() / 2U] ^ 0x5A);
    std::ofstream output(path.string() + ".corrupt", std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  const auto corrupt = resumed.value()->load_checkpoint(path.string() + ".corrupt");
  require(!corrupt.has_value() && corrupt.error() == pb::TrainerError::IntegrityFailure,
          "corrupted checkpoint rejected");
  auto other_config = config;
  other_config.training_seed += 1U;
  auto other = pb::Trainer::create(game.value(), resources.view(), other_config);
  require(other.has_value(), "trainer with another seed creates");
  const auto mismatch = other.value()->load_checkpoint(path);
  require(!mismatch.has_value() && mismatch.error() == pb::TrainerError::IntegrityFailure,
          "checkpoint of another identity rejected");
  std::cout << "resume: checkpoint " << std::filesystem::file_size(path) << " bytes, fingerprint "
            << resumed.value()->state_fingerprint() << '\n';
}

void test_sampled_estimator(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  auto boards = oracle_boards();
  boards.sample = true;
  auto config = resources.config();
  config.threads = 4U;
  config.batch_boards = 6U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config, &boards);
  require(trainer.has_value(), "list-sampling trainer creates");
  for (int iteration = 0; iteration < 12; ++iteration) {
    require(trainer.value()->iterate().has_value(), "iteration succeeds");
  }
  const auto exact = trainer.value()->estimate_exploitability(0U, true);
  require(exact.has_value() && exact.value().exact && exact.value().boards == 3U,
          "exact evaluation over the listed boards");
  require(exact.value().gain[0] >= -1e-9 && exact.value().gain[1] >= -1e-9,
          "best response never loses to the average strategy");
  const auto sampled = trainer.value()->estimate_exploitability(400U);
  require(sampled.has_value() && !sampled.value().exact && sampled.value().boards == 400U,
          "sampled evaluation over 400 boards");
  for (std::uint8_t player = 0; player < 2U; ++player) {
    const auto error = std::abs(sampled.value().gain[player] - exact.value().gain[player]);
    require(error <= 3.0 * sampled.value().gain_standard_error[player] + 1e-9,
            "sampled gain is within three standard errors of the exact gain");
  }
  std::cout << "estimator: exact gains [" << exact.value().gain[0] << ", " << exact.value().gain[1]
            << "], sampled [" << sampled.value().gain[0] << " +- "
            << sampled.value().gain_standard_error[0] << ", " << sampled.value().gain[1] << " +- "
            << sampled.value().gain_standard_error[1] << "]\n";
}

void test_exploitability_decreases(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  auto config = resources.config();
  config.threads = 4U;
  config.batch_boards = 16U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
  require(trainer.has_value(), "trainer creates");
  std::vector<double> curve;
  double training_seconds = 0.0;
  for (int checkpoint = 0; checkpoint <= 3; ++checkpoint) {
    const auto estimate = trainer.value()->estimate_exploitability(300U);
    require(estimate.has_value(), "estimate succeeds");
    curve.push_back(estimate.value().max_gain);
    std::cout << "curve: iteration " << trainer.value()->iteration() << " max gain "
              << estimate.value().max_gain << " +- " << estimate.value().max_gain_half_width
              << " nashconv " << estimate.value().nashconv << " (training " << training_seconds
              << " s, evaluation " << estimate.value().seconds << " s)\n";
    if (checkpoint == 3) {
      break;
    }
    for (int iteration = 0; iteration < 20; ++iteration) {
      const auto telemetry = trainer.value()->iterate();
      require(telemetry.has_value(), "iteration succeeds");
      training_seconds += telemetry.value().seconds;
    }
  }
  require(curve.back() < curve.front(), "exploitability estimate decreases with training");
  require(curve.front() > 0.1, "the uniform strategy is clearly exploitable");
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
              << (resources.buckets_loaded ? "loaded" : "built") << " ("
              << resources.flop->capacity() << "/" << resources.turn->capacity() << "/"
              << resources.river->capacity() << ")\n";
    test_finite_game_oracle(resources);
    test_determinism(resources);
    test_resume(resources);
    test_sampled_estimator(resources);
    test_exploitability_decreases(resources);
    std::cout << "PREFLOP_BLUEPRINT_TRAINER_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAINER_TESTS=FAIL " << error.what() << " after " << assertions
              << " assertions\n";
    return 1;
  }
}
