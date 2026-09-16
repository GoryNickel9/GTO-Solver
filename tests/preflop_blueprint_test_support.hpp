#pragma once

// Shared helpers of the preflop blueprint trainer and certifier tests:
// resource loading (with synthetic fallbacks), fixtures, the reduced oracle
// game and the FiniteGame builder (bucket and lossless information sets).
#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/exact_features.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/preflop_blueprint/best_response.hpp"
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

namespace pb_test {

namespace ca = gtosd::card_abstraction;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;

inline std::uint64_t assertions = 0U;

inline void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

inline bool close(const double left, const double right, const double tolerance) {
  return std::abs(left - right) <= tolerance * std::max(1.0, std::abs(right));
}

inline std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

inline pb::GameConfig load_fixture(const std::string_view name) {
  const auto path =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "fixtures" / std::string(name);
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), std::string("fixture parses: ") + std::string(name));
  return parsed.value();
}

inline gtosd::CardId card(const std::string_view text) {
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

inline ca::OpponentGroups synthetic_groups() {
  std::array<std::uint8_t, 81> ranking{};
  std::array<double, 81> equity{};
  for (std::uint8_t index = 0U; index < ranking.size(); ++index) {
    ranking[index] = index;
    equity[index] = 1.0 - static_cast<double>(index) / 81.0;
  }
  return ca::OpponentGroups::from_ranking(ranking, equity, "synthetic_trainer_test_groups");
}

inline Resources load_resources(const std::filesystem::path &resources_dir,
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

inline ca::BoardHistory make_history(const std::array<std::string_view, 5> &texts) {
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
inline pb::TrainingBoards oracle_boards() {
  pb::TrainingBoards boards;
  boards.histories = {make_history({"6s", "7d", "8c", "9h", "As"}),
                      make_history({"6d", "6h", "9s", "7c", "Ad"}),
                      make_history({"7s", "8h", "9c", "6c", "Ah"})};
  boards.weights = {1.0, 2.0, 3.0};
  boards.sample = false;
  return boards;
}

inline std::vector<std::uint16_t> combos_from_cards(const std::array<std::string_view, 4> &texts) {
  std::vector<std::uint16_t> combos;
  for (std::size_t first = 0; first < texts.size(); ++first) {
    for (std::size_t second = first + 1U; second < texts.size(); ++second) {
      combos.push_back(ca::combo_index(card(texts[first]), card(texts[second])));
    }
  }
  return combos;
}

inline pb::HandSubsets oracle_subsets() {
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
  // Bucket mode keys the information sets by (player, node, bucket row): the
  // abstract game solved by the trainer. Lossless mode keys them by (player,
  // node, combo, public cards dealt so far): the physical game restricted to
  // the listed boards, whose exact best response is the physical one. With an
  // average policy the builder also records the lifted strategy profile.
  FiniteGameBuilder(const pb::CompiledGame &game, const Resources &resources,
                    const bool lossless = false, const pb::BucketPolicy *average = nullptr)
      : game_(game), resources_(resources), lossless_(lossless), average_(average) {}

  [[nodiscard]] const gtosd::StrategyProfile &profile() const noexcept { return profile_; }

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
    const auto row = context.row(node.street, acting_hand);
    if (lossless_) {
      std::string key = "p" + std::to_string(node.actor) + "|n" + std::to_string(node_id) + "|c" +
                        std::to_string(context.combo_ids()[acting_hand]);
      const auto &history = context.history();
      if (node.street >= gtosd::Street::Flop) {
        key += "|F" + std::to_string(history.flop[0].value()) + "." +
               std::to_string(history.flop[1].value()) + "." +
               std::to_string(history.flop[2].value());
      }
      if (node.street >= gtosd::Street::Turn) {
        key += "|T" + std::to_string(history.turn.value());
      }
      if (node.street >= gtosd::Street::River) {
        key += "|R" + std::to_string(history.river.value());
      }
      finite.information_set = key;
    } else {
      finite.information_set = information_set(node.actor, node_id, row);
    }
    if (average_ != nullptr) {
      gtosd::InformationSetStrategy strategy;
      strategy.player = node.actor;
      const auto probabilities = average_->row(node_id, row);
      for (std::size_t action = 0; action < probabilities.size(); ++action) {
        strategy.actions.push_back(static_cast<gtosd::GameActionId>(action));
        strategy.probabilities.push_back(probabilities[action]);
      }
      profile_[finite.information_set] = std::move(strategy);
    }
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
  bool lossless_{false};
  const pb::BucketPolicy *average_{nullptr};
  gtosd::StrategyProfile profile_;
  std::vector<gtosd::GameNode> nodes_;
};

} // namespace pb_test
