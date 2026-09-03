#include "gtosd/tree/tree.hpp"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <string_view>

namespace gtosd {
namespace {

constexpr std::uint64_t full_deck_mask = (std::uint64_t{1} << 36U) - 1U;

const ScenarioConfig &scenario_config(const PostflopTreeConfig &config, const Street street,
                                      const Player player, const BettingScenario scenario) {
  const auto street_index =
      static_cast<std::size_t>(street) - static_cast<std::size_t>(Street::Flop);
  return config.streets[street_index]
      .players[static_cast<std::size_t>(player)][static_cast<std::size_t>(scenario)];
}

ActionConfig to_action_config(const ScenarioConfig &source) {
  ActionConfig result;
  result.aggressive_sizes = source.aggressive_sizes;
  result.raise_depth = source.raise_depth;
  result.all_in_mode = source.all_in_mode;
  result.all_in_threshold = source.all_in_threshold;
  result.all_in_strict_boundary = source.all_in_strict_boundary;
  result.minimum_bet = source.minimum_bet;
  result.aggressive_sizes_by_raise_count = source.aggressive_sizes_by_raise_count;
  result.aggressive_target_rounding = source.aggressive_target_rounding;
  result.aggressive_target_rounding_mode = source.aggressive_target_rounding_mode;
  return result;
}

BettingScenario current_scenario(const PublicState &state) {
  if (amount_to_call(state, state.player_to_act).units() > 0) {
    return BettingScenario::FacingBet;
  }
  return state.player_to_act == static_cast<std::uint8_t>(Player::CO) ? BettingScenario::Lead
                                                                      : BettingScenario::AfterCheck;
}

std::vector<CardId> board_cards(const std::uint64_t board_mask) {
  std::vector<CardId> board;
  board.reserve(static_cast<std::size_t>(std::popcount(board_mask)));
  for (std::uint8_t index = 0; index < 36U; ++index) {
    if ((board_mask & (std::uint64_t{1} << index)) != 0U) {
      board.push_back(CardId::from_index(index).value());
    }
  }
  return board;
}

Result<PublicState, TreeError> advance_chance_state(const PublicState &state, const CardId card) {
  PublicState next;
  if (state.status == HandStatus::StreetComplete) {
    const auto advanced = advance_street(state);
    if (!advanced) {
      return Result<PublicState, TreeError>::failure(TreeError::GameFailure);
    }
    next = advanced.value();
  } else if (state.status == HandStatus::AllInRunout && state.street != Street::River) {
    next = state;
    next.street = static_cast<Street>(static_cast<std::uint8_t>(state.street) + 1U);
    next.status = next.street == Street::River ? HandStatus::Showdown : HandStatus::AllInRunout;
    next.current_bet = Money{};
    next.last_full_raise_increment = Money{};
    next.committed_this_street = {};
    next.acted_players_mask = 0;
    next.raise_count_this_street = 0;
  } else {
    return Result<PublicState, TreeError>::failure(TreeError::GameFailure);
  }
  next.board_mask |= card.mask();
  if (!validate_state(next)) {
    return Result<PublicState, TreeError>::failure(TreeError::GameFailure);
  }
  return Result<PublicState, TreeError>::success(next);
}

class TreeBuilder {
public:
  TreeBuilder(const PostflopTreeConfig &config, const TreeBuildOptions &options,
              const PublicTreeStreamConsumer *const stream_consumer = nullptr)
      : options_(options), stream_consumer_(stream_consumer) {
    tree_.config = config;
  }

  Result<PublicTree, TreeError> build() {
    if (options_.reserve_nodes > options_.maximum_nodes ||
        options_.reserve_nodes > std::numeric_limits<std::size_t>::max()) {
      return Result<PublicTree, TreeError>::failure(TreeError::BuildLimitExceeded);
    }
    if (stream_consumer_ == nullptr) {
      tree_.nodes.reserve(static_cast<std::size_t>(options_.reserve_nodes));
    } else if (!stream_consumer_->node || !stream_consumer_->edges) {
      return Result<PublicTree, TreeError>::failure(TreeError::InvalidConfiguration);
    }
    const auto board = configured_board(tree_.config);
    const auto mask = card_mask(board);
    if (!mask || board.size() < 3U || board.size() > 5U ||
        std::popcount(mask.value()) != static_cast<int>(board.size())) {
      return Result<PublicTree, TreeError>::failure(TreeError::InvalidBoard);
    }
    const auto state =
        make_hu_postflop_state(configured_starting_street(tree_.config), tree_.config.initial_pot,
                               tree_.config.effective_stack, mask.value());
    if (!state) {
      return Result<PublicTree, TreeError>::failure(TreeError::GameFailure);
    }
    Stabilizer root_stabilizer;
    if (!options_.canonical_chance_permutations.empty()) {
      if (options_.canonical_chance_permutations.size() > 24U) {
        return Result<PublicTree, TreeError>::failure(TreeError::InvalidConfiguration);
      }
      for (std::size_t index = 0; index < options_.canonical_chance_permutations.size(); ++index) {
        root_stabilizer.push_back(static_cast<std::uint8_t>(index));
      }
    }
    const auto root = expand(state.value(), 0, root_stabilizer);
    if (!root) {
      return Result<PublicTree, TreeError>::failure(root.error());
    }
    tree_.root = root.value();
    tree_.stats.estimated_eager_bytes = tree_.stats.node_count * sizeof(PublicTreeNode) +
                                        tree_.stats.edge_count * sizeof(PublicTreeEdge);
    if (stream_consumer_ == nullptr) {
      tree_.betting_tree_hash = public_tree_hash(tree_);
    }
    return Result<PublicTree, TreeError>::success(std::move(tree_));
  }

private:
  using Stabilizer = std::vector<std::uint8_t>;

  CardId transform_suit(const CardId card, const std::uint8_t permutation) const {
    const auto suit =
        options_.canonical_chance_permutations[permutation][static_cast<std::size_t>(card.suit())];
    return CardId::from_parts(card.rank(), static_cast<Suit>(suit));
  }

  Result<NodeId, TreeError> add_node(const PublicNodeKind kind, const PublicState &state,
                                     const std::uint32_t depth, const Stabilizer &stabilizer) {
    if (tree_.stats.node_count >= options_.maximum_nodes) {
      return Result<NodeId, TreeError>::failure(TreeError::BuildLimitExceeded);
    }
    if (tree_.stats.node_count >= std::numeric_limits<NodeId>::max()) {
      return Result<NodeId, TreeError>::failure(TreeError::NodeOverflow);
    }
    const auto id = static_cast<NodeId>(tree_.stats.node_count);
    if (stream_consumer_ != nullptr) {
      if (!stream_consumer_->node(id, kind, state, depth) ||
          (stream_consumer_->stabilizer && !stream_consumer_->stabilizer(id, stabilizer))) {
        return Result<NodeId, TreeError>::failure(TreeError::StreamConsumerFailure);
      }
    } else {
      tree_.nodes.push_back(PublicTreeNode{id, kind, state, depth, {}});
    }
    ++tree_.stats.node_count;
    ++tree_.stats.node_count_by_street[static_cast<std::size_t>(state.street) -
                                       static_cast<std::size_t>(Street::Flop)];
    tree_.stats.maximum_depth = std::max(tree_.stats.maximum_depth, depth);
    switch (kind) {
    case PublicNodeKind::Decision:
      ++tree_.stats.decision_nodes;
      ++tree_.stats.decision_nodes_by_street[static_cast<std::size_t>(state.street) -
                                             static_cast<std::size_t>(Street::Flop)];
      break;
    case PublicNodeKind::Chance:
      ++tree_.stats.chance_nodes;
      ++tree_.stats.chance_nodes_by_street[static_cast<std::size_t>(state.street) -
                                           static_cast<std::size_t>(Street::Flop)];
      break;
    case PublicNodeKind::TerminalFold:
      ++tree_.stats.terminal_fold_nodes;
      break;
    case PublicNodeKind::TerminalShowdown:
      ++tree_.stats.terminal_showdown_nodes;
      break;
    }
    return Result<NodeId, TreeError>::success(id);
  }

  Result<NodeId, TreeError> expand(const PublicState &state, const std::uint32_t depth,
                                   const Stabilizer &stabilizer) {
    if (!validate_state(state)) {
      return Result<NodeId, TreeError>::failure(TreeError::GameFailure);
    }
    if (state.status == HandStatus::Folded) {
      const auto settlement = settle_terminal(state, tree_.config.rake);
      if (!settlement) {
        return Result<NodeId, TreeError>::failure(TreeError::SettlementFailure);
      }
      return add_node(PublicNodeKind::TerminalFold, state, depth, stabilizer);
    }
    if (state.status == HandStatus::Showdown) {
      if (state.street != Street::River || std::popcount(state.board_mask) != 5) {
        return Result<NodeId, TreeError>::failure(TreeError::InvalidBoard);
      }
      return add_node(PublicNodeKind::TerminalShowdown, state, depth, stabilizer);
    }
    if (state.status == HandStatus::StreetComplete || state.status == HandStatus::AllInRunout) {
      return expand_chance(state, depth, stabilizer);
    }
    if (state.status != HandStatus::InProgress) {
      return Result<NodeId, TreeError>::failure(TreeError::GameFailure);
    }
    return expand_decision(state, depth, stabilizer);
  }

  Result<NodeId, TreeError> expand_decision(const PublicState &state, const std::uint32_t depth,
                                            const Stabilizer &stabilizer) {
    const auto node = add_node(PublicNodeKind::Decision, state, depth, stabilizer);
    if (!node) {
      return node;
    }
    const auto actor = static_cast<Player>(state.player_to_act);
    const auto &source =
        scenario_config(tree_.config, state.street, actor, current_scenario(state));
    const auto actions = legal_actions(state, to_action_config(source));
    if (!actions || actions.value().empty()) {
      return Result<NodeId, TreeError>::failure(TreeError::GameFailure);
    }
    if (actions.value().size() >= PublicTreeStats::decision_action_bucket_count) {
      return Result<NodeId, TreeError>::failure(TreeError::NodeOverflow);
    }
    ++tree_.stats
          .decision_nodes_by_street_player_action[static_cast<std::size_t>(state.street) -
                                                  static_cast<std::size_t>(Street::Flop)]
                                                 [state.player_to_act][actions.value().size()];

    std::vector<PublicTreeEdge> edges;
    edges.reserve(actions.value().size());
    for (const auto &action : actions.value()) {
      const auto successor = apply_action(state, action, to_action_config(source));
      if (!successor) {
        return Result<NodeId, TreeError>::failure(TreeError::GameFailure);
      }
      const auto child = expand(successor.value(), depth + 1U, stabilizer);
      if (!child) {
        return child;
      }
      PublicTreeEdge edge;
      edge.kind = PublicEdgeKind::Action;
      edge.child = child.value();
      edge.action = action;
      edges.push_back(edge);
    }
    tree_.stats.edge_count += edges.size();
    const auto street_index =
        static_cast<std::size_t>(state.street) - static_cast<std::size_t>(Street::Flop);
    tree_.stats.edge_count_by_street[street_index] += edges.size();
    tree_.stats.action_edges_by_street[street_index] += edges.size();
    if (stream_consumer_ != nullptr) {
      if (!stream_consumer_->edges(node.value(), edges)) {
        return Result<NodeId, TreeError>::failure(TreeError::StreamConsumerFailure);
      }
    } else {
      tree_.nodes[static_cast<std::size_t>(node.value())].edges = std::move(edges);
    }
    return node;
  }

  Result<NodeId, TreeError> expand_chance(const PublicState &state, const std::uint32_t depth,
                                          const Stabilizer &stabilizer) {
    if (state.street == Street::River || std::popcount(state.board_mask) < 3 ||
        std::popcount(state.board_mask) >= 5) {
      return Result<NodeId, TreeError>::failure(TreeError::InvalidBoard);
    }
    const auto node = add_node(PublicNodeKind::Chance, state, depth, stabilizer);
    if (!node) {
      return node;
    }
    const auto available_mask = full_deck_mask ^ state.board_mask;
    const auto total_outcomes = static_cast<std::uint32_t>(std::popcount(available_mask));
    std::vector<PublicTreeEdge> edges;
    edges.reserve(stabilizer.empty() ? total_outcomes : total_outcomes / 2U + 1U);
    std::array<bool, 36U> consumed{};
    for (std::uint8_t index = 0; index < 36U; ++index) {
      const auto card = CardId::from_index(index).value();
      if ((available_mask & card.mask()) == 0U || consumed[index]) {
        continue;
      }
      CardId representative = card;
      if (!stabilizer.empty()) {
        for (const auto permutation : stabilizer) {
          const auto transformed = transform_suit(card, permutation);
          if ((available_mask & transformed.mask()) == 0U) {
            return Result<NodeId, TreeError>::failure(TreeError::InvalidConfiguration);
          }
          representative = std::min(representative, transformed);
        }
      }
      if (representative != card) {
        continue;
      }
      Stabilizer child_stabilizer;
      for (const auto permutation : stabilizer) {
        if (transform_suit(representative, permutation) == representative) {
          child_stabilizer.push_back(permutation);
        }
      }
      const auto successor = advance_chance_state(state, representative);
      if (!successor) {
        return Result<NodeId, TreeError>::failure(successor.error());
      }
      const auto child = expand(successor.value(), depth + 1U, child_stabilizer);
      if (!child) {
        return child;
      }
      PublicTreeEdge edge;
      edge.kind = PublicEdgeKind::ChanceCard;
      edge.child = child.value();
      edge.chance_card = representative;
      edge.total_legal_outcome_count = total_outcomes;
      if (stabilizer.empty()) {
        edge.chance_outcomes[0] = {representative, 0U};
        edge.chance_outcome_count = 1U;
      } else {
        for (std::uint8_t physical_index = 0; physical_index < 36U; ++physical_index) {
          const auto physical = CardId::from_index(physical_index).value();
          if ((available_mask & physical.mask()) == 0U || consumed[physical_index]) {
            continue;
          }
          std::uint8_t mapping = 0U;
          bool in_orbit = physical == representative;
          if (!in_orbit) {
            for (const auto permutation : stabilizer) {
              if (transform_suit(physical, permutation) == representative) {
                mapping = permutation;
                in_orbit = true;
                break;
              }
            }
          }
          if (!in_orbit) {
            continue;
          }
          if (edge.chance_outcome_count >= edge.chance_outcomes.size()) {
            return Result<NodeId, TreeError>::failure(TreeError::NodeOverflow);
          }
          edge.chance_outcomes[edge.chance_outcome_count++] = {physical, mapping};
          consumed[physical_index] = true;
        }
      }
      edge.physical_outcome_count = edge.chance_outcome_count;
      edges.push_back(edge);
    }
    tree_.stats.edge_count += edges.size();
    tree_.stats.chance_edges += edges.size();
    const auto street_index =
        static_cast<std::size_t>(state.street) - static_cast<std::size_t>(Street::Flop);
    tree_.stats.edge_count_by_street[street_index] += edges.size();
    tree_.stats.chance_edges_by_street[street_index] += edges.size();
    if (stream_consumer_ != nullptr) {
      if (!stream_consumer_->edges(node.value(), edges)) {
        return Result<NodeId, TreeError>::failure(TreeError::StreamConsumerFailure);
      }
    } else {
      tree_.nodes[static_cast<std::size_t>(node.value())].edges = std::move(edges);
    }
    return node;
  }

  TreeBuildOptions options_;
  const PublicTreeStreamConsumer *stream_consumer_{nullptr};
  PublicTree tree_;
};

class TreeEstimator {
public:
  TreeEstimator(const PostflopTreeConfig &config, const TreeBuildOptions &options)
      : config_(config), options_(options) {}

  Result<PublicTreeStats, TreeError> estimate() {
    const auto board = configured_board(config_);
    const auto mask = card_mask(board);
    if (!mask || board.size() < 3U || board.size() > 5U ||
        std::popcount(mask.value()) != static_cast<int>(board.size())) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::InvalidBoard);
    }
    const auto state =
        make_hu_postflop_state(configured_starting_street(config_), config_.initial_pot,
                               config_.effective_stack, mask.value());
    if (!state) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::GameFailure);
    }
    const auto counted = count(state.value(), 0);
    if (!counted) {
      return Result<PublicTreeStats, TreeError>::failure(counted.error());
    }
    auto stats = counted.value();
    if (stats.node_count > options_.maximum_nodes ||
        stats.node_count > std::numeric_limits<std::uint64_t>::max() / sizeof(PublicTreeNode) ||
        stats.edge_count > std::numeric_limits<std::uint64_t>::max() / sizeof(PublicTreeEdge)) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::BuildLimitExceeded);
    }
    const auto node_bytes = stats.node_count * sizeof(PublicTreeNode);
    const auto edge_bytes = stats.edge_count * sizeof(PublicTreeEdge);
    if (node_bytes > std::numeric_limits<std::uint64_t>::max() - edge_bytes) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::NodeOverflow);
    }
    stats.estimated_eager_bytes = node_bytes + edge_bytes;
    return Result<PublicTreeStats, TreeError>::success(stats);
  }

private:
  static bool checked_add(std::uint64_t &target, const std::uint64_t value) {
    if (target > std::numeric_limits<std::uint64_t>::max() - value) {
      return false;
    }
    target += value;
    return true;
  }

  static bool add_scaled(PublicTreeStats &target, const PublicTreeStats &source,
                         const std::uint64_t multiplier) {
    const auto add_field = [multiplier](std::uint64_t &destination, const std::uint64_t value) {
      return value == 0U || (multiplier <= std::numeric_limits<std::uint64_t>::max() / value &&
                             checked_add(destination, value * multiplier));
    };
    if (!add_field(target.node_count, source.node_count) ||
        !add_field(target.edge_count, source.edge_count) ||
        !add_field(target.decision_nodes, source.decision_nodes) ||
        !add_field(target.chance_nodes, source.chance_nodes) ||
        !add_field(target.terminal_fold_nodes, source.terminal_fold_nodes) ||
        !add_field(target.terminal_showdown_nodes, source.terminal_showdown_nodes) ||
        !add_field(target.chance_edges, source.chance_edges)) {
      return false;
    }
    for (std::size_t street = 0; street < 3U; ++street) {
      if (!add_field(target.node_count_by_street[street], source.node_count_by_street[street]) ||
          !add_field(target.edge_count_by_street[street], source.edge_count_by_street[street]) ||
          !add_field(target.decision_nodes_by_street[street],
                     source.decision_nodes_by_street[street]) ||
          !add_field(target.chance_edges_by_street[street],
                     source.chance_edges_by_street[street]) ||
          !add_field(target.action_edges_by_street[street],
                     source.action_edges_by_street[street])) {
        return false;
      }
      if (!add_field(target.chance_nodes_by_street[street],
                     source.chance_nodes_by_street[street])) {
        return false;
      }
      for (std::size_t player = 0; player < 2U; ++player) {
        for (std::size_t actions = 0; actions < PublicTreeStats::decision_action_bucket_count;
             ++actions) {
          if (!add_field(target.decision_nodes_by_street_player_action[street][player][actions],
                         source.decision_nodes_by_street_player_action[street][player][actions])) {
            return false;
          }
        }
      }
    }
    target.maximum_depth = std::max(target.maximum_depth, source.maximum_depth);
    return true;
  }

  static PublicTreeStats one_node(const PublicNodeKind kind, const PublicState &state,
                                  const std::uint32_t depth) {
    PublicTreeStats stats;
    stats.node_count = 1;
    stats.node_count_by_street[static_cast<std::size_t>(state.street) -
                               static_cast<std::size_t>(Street::Flop)] = 1;
    stats.maximum_depth = depth;
    switch (kind) {
    case PublicNodeKind::Decision:
      stats.decision_nodes = 1;
      stats.decision_nodes_by_street[static_cast<std::size_t>(state.street) -
                                     static_cast<std::size_t>(Street::Flop)] = 1;
      break;
    case PublicNodeKind::Chance:
      stats.chance_nodes = 1;
      stats.chance_nodes_by_street[static_cast<std::size_t>(state.street) -
                                   static_cast<std::size_t>(Street::Flop)] = 1;
      break;
    case PublicNodeKind::TerminalFold:
      stats.terminal_fold_nodes = 1;
      break;
    case PublicNodeKind::TerminalShowdown:
      stats.terminal_showdown_nodes = 1;
      break;
    }
    return stats;
  }

  Result<PublicTreeStats, TreeError> count(const PublicState &state, const std::uint32_t depth) {
    if (!validate_state(state)) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::GameFailure);
    }
    if (state.status == HandStatus::Folded) {
      if (!settle_terminal(state, config_.rake)) {
        return Result<PublicTreeStats, TreeError>::failure(TreeError::SettlementFailure);
      }
      return Result<PublicTreeStats, TreeError>::success(
          one_node(PublicNodeKind::TerminalFold, state, depth));
    }
    if (state.status == HandStatus::Showdown) {
      if (state.street != Street::River || std::popcount(state.board_mask) != 5) {
        return Result<PublicTreeStats, TreeError>::failure(TreeError::InvalidBoard);
      }
      return Result<PublicTreeStats, TreeError>::success(
          one_node(PublicNodeKind::TerminalShowdown, state, depth));
    }
    if (state.status == HandStatus::StreetComplete || state.status == HandStatus::AllInRunout) {
      return count_chance(state, depth);
    }
    if (state.status != HandStatus::InProgress) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::GameFailure);
    }

    auto stats = one_node(PublicNodeKind::Decision, state, depth);
    const auto actor = static_cast<Player>(state.player_to_act);
    const auto &source = scenario_config(config_, state.street, actor, current_scenario(state));
    const auto action_config = to_action_config(source);
    const auto actions = legal_actions(state, action_config);
    if (!actions || actions.value().empty()) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::GameFailure);
    }
    const auto action_count = static_cast<std::uint64_t>(actions.value().size());
    if (action_count >= PublicTreeStats::decision_action_bucket_count) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::NodeOverflow);
    }
    ++stats.decision_nodes_by_street_player_action[static_cast<std::size_t>(state.street) -
                                                   static_cast<std::size_t>(Street::Flop)]
                                                  [state.player_to_act]
                                                  [static_cast<std::size_t>(action_count)];
    stats.edge_count = action_count;
    const auto street_index =
        static_cast<std::size_t>(state.street) - static_cast<std::size_t>(Street::Flop);
    stats.edge_count_by_street[street_index] = action_count;
    stats.action_edges_by_street[street_index] = action_count;
    for (const auto &action : actions.value()) {
      const auto successor = apply_action(state, action, action_config);
      if (!successor) {
        return Result<PublicTreeStats, TreeError>::failure(TreeError::GameFailure);
      }
      const auto child = count(successor.value(), depth + 1U);
      if (!child) {
        return child;
      }
      if (!add_scaled(stats, child.value(), 1U)) {
        return Result<PublicTreeStats, TreeError>::failure(TreeError::NodeOverflow);
      }
    }
    if (stats.node_count > options_.maximum_nodes) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::BuildLimitExceeded);
    }
    return Result<PublicTreeStats, TreeError>::success(stats);
  }

  Result<PublicTreeStats, TreeError> count_chance(const PublicState &state,
                                                  const std::uint32_t depth) {
    if (state.street == Street::River || std::popcount(state.board_mask) < 3 ||
        std::popcount(state.board_mask) >= 5) {
      return Result<PublicTreeStats, TreeError>::failure(TreeError::InvalidBoard);
    }
    auto stats = one_node(PublicNodeKind::Chance, state, depth);
    const auto available_mask = full_deck_mask ^ state.board_mask;
    const auto outcomes = static_cast<std::uint64_t>(std::popcount(available_mask));
    stats.edge_count = outcomes;
    stats.chance_edges = outcomes;
    const auto street_index =
        static_cast<std::size_t>(state.street) - static_cast<std::size_t>(Street::Flop);
    stats.edge_count_by_street[street_index] = outcomes;
    stats.chance_edges_by_street[street_index] = outcomes;
    for (std::uint8_t index = 0; index < 36U; ++index) {
      const auto card = CardId::from_index(index).value();
      if ((available_mask & card.mask()) == 0U) {
        continue;
      }
      const auto successor = advance_chance_state(state, card);
      if (!successor) {
        return Result<PublicTreeStats, TreeError>::failure(successor.error());
      }
      const auto child = count(successor.value(), depth + 1U);
      if (!child) {
        return child;
      }
      if (!add_scaled(stats, child.value(), outcomes)) {
        return Result<PublicTreeStats, TreeError>::failure(TreeError::NodeOverflow);
      }
      if (stats.node_count > options_.maximum_nodes) {
        return Result<PublicTreeStats, TreeError>::failure(TreeError::BuildLimitExceeded);
      }
      break;
    }
    return Result<PublicTreeStats, TreeError>::success(stats);
  }

  const PostflopTreeConfig &config_;
  TreeBuildOptions options_;
};

void hash_byte(std::uint64_t &hash, const std::uint8_t value) {
  constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
  hash ^= value;
  hash *= fnv_prime;
}

void hash_integer(std::uint64_t &hash, const std::uint64_t value) {
  for (std::uint8_t offset = 0; offset < 8U; ++offset) {
    hash_byte(hash, static_cast<std::uint8_t>((value >> (offset * 8U)) & 0xFFU));
  }
}

void hash_text(std::uint64_t &hash, const std::string_view text) {
  hash_integer(hash, text.size());
  for (const auto character : text) {
    hash_byte(hash, static_cast<std::uint8_t>(character));
  }
}

} // namespace

Result<PublicTree, TreeError> build_public_tree(const PostflopTreeConfig &config,
                                                const TreeBuildOptions &options) {
  if (!validate_tree_config(config) || options.maximum_nodes == 0U) {
    return Result<PublicTree, TreeError>::failure(TreeError::InvalidConfiguration);
  }
  try {
    return TreeBuilder(config, options).build();
  } catch (const std::bad_alloc &) {
    return Result<PublicTree, TreeError>::failure(TreeError::BuildLimitExceeded);
  }
}

Result<PublicTreeStreamResult, TreeError>
stream_public_tree(const PostflopTreeConfig &config, const PublicTreeStreamConsumer &consumer,
                   const TreeBuildOptions &options) {
  const auto valid = validate_tree_config(config);
  if (!valid) {
    return Result<PublicTreeStreamResult, TreeError>::failure(TreeError::InvalidConfiguration);
  }
  TreeBuilder builder(config, options, &consumer);
  auto built = builder.build();
  if (!built) {
    return Result<PublicTreeStreamResult, TreeError>::failure(built.error());
  }
  return Result<PublicTreeStreamResult, TreeError>::success(
      PublicTreeStreamResult{built.value().root, built.value().stats});
}

Result<PublicTreeStats, TreeError> estimate_public_tree(const PostflopTreeConfig &config,
                                                        const TreeBuildOptions &options) {
  if (!validate_tree_config(config) || options.maximum_nodes == 0U) {
    return Result<PublicTreeStats, TreeError>::failure(TreeError::InvalidConfiguration);
  }
  return TreeEstimator(config, options).estimate();
}

Result<std::vector<ConditionedChanceEdge>, TreeError>
condition_chance_edges(const PublicTree &tree, const NodeId chance_node,
                       const std::vector<CardId> &private_or_dead_cards) {
  if (chance_node >= tree.nodes.size()) {
    return Result<std::vector<ConditionedChanceEdge>, TreeError>::failure(TreeError::InvalidNode);
  }
  const auto &node = tree.nodes[static_cast<std::size_t>(chance_node)];
  if (node.kind != PublicNodeKind::Chance) {
    return Result<std::vector<ConditionedChanceEdge>, TreeError>::failure(TreeError::NotChanceNode);
  }
  const auto dead_mask = card_mask(private_or_dead_cards);
  if (!dead_mask || (dead_mask.value() & node.state.board_mask) != 0U) {
    return Result<std::vector<ConditionedChanceEdge>, TreeError>::failure(
        TreeError::InvalidPrivateCards);
  }

  std::vector<ConditionedChanceEdge> result;
  result.reserve(node.edges.size());
  for (const auto &edge : node.edges) {
    if (edge.kind != PublicEdgeKind::ChanceCard ||
        (edge.chance_card.mask() & dead_mask.value()) != 0U) {
      continue;
    }
    result.push_back({edge.child, edge.chance_card, edge.physical_outcome_count, 0});
  }
  if (result.empty()) {
    return Result<std::vector<ConditionedChanceEdge>, TreeError>::failure(
        TreeError::InvalidPrivateCards);
  }
  const auto total = static_cast<std::uint32_t>(result.size());
  for (auto &edge : result) {
    edge.total_legal_outcome_count = total;
  }
  return Result<std::vector<ConditionedChanceEdge>, TreeError>::success(std::move(result));
}

Result<TerminalResolution, TreeError>
resolve_showdown_terminal(const PublicTree &tree, const NodeId terminal_node,
                          const std::vector<std::array<CardId, 2>> &hole_cards) {
  if (terminal_node >= tree.nodes.size()) {
    return Result<TerminalResolution, TreeError>::failure(TreeError::InvalidNode);
  }
  const auto &node = tree.nodes[static_cast<std::size_t>(terminal_node)];
  if (node.kind != PublicNodeKind::TerminalShowdown || node.state.status != HandStatus::Showdown) {
    return Result<TerminalResolution, TreeError>::failure(TreeError::NotShowdownTerminal);
  }
  if (hole_cards.size() != node.state.player_count) {
    return Result<TerminalResolution, TreeError>::failure(TreeError::InvalidPrivateCards);
  }
  const auto board = board_cards(node.state.board_mask);
  auto all_cards = board;
  all_cards.reserve(board.size() + hole_cards.size() * 2U);
  for (const auto &hand : hole_cards) {
    all_cards.push_back(hand[0]);
    all_cards.push_back(hand[1]);
  }
  if (!card_mask(all_cards)) {
    return Result<TerminalResolution, TreeError>::failure(TreeError::InvalidPrivateCards);
  }
  const auto showdown = evaluate_showdown(hole_cards, board);
  if (!showdown) {
    return Result<TerminalResolution, TreeError>::failure(TreeError::EquityFailure);
  }
  const auto settlement =
      settle_terminal(node.state, tree.config.rake, showdown.value().winner_mask);
  if (!settlement) {
    return Result<TerminalResolution, TreeError>::failure(TreeError::SettlementFailure);
  }
  return Result<TerminalResolution, TreeError>::success(
      TerminalResolution{showdown.value(), settlement.value()});
}

std::string public_tree_hash(const PublicTree &tree) {
  constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
  std::uint64_t hash = fnv_offset_basis;
  hash_integer(hash, PublicTree::format_major);
  hash_integer(hash, PublicTree::format_minor);
  hash_integer(hash, tree.root);
  hash_integer(hash, tree.nodes.size());
  for (const auto &node : tree.nodes) {
    hash_integer(hash, node.id);
    hash_integer(hash, static_cast<std::uint8_t>(node.kind));
    hash_integer(hash, node.depth);
    hash_text(hash, serialize_public_state(node.state));
    hash_integer(hash, node.edges.size());
    for (const auto &edge : node.edges) {
      hash_integer(hash, static_cast<std::uint8_t>(edge.kind));
      hash_integer(hash, edge.child);
      if (edge.kind == PublicEdgeKind::Action) {
        hash_integer(hash, static_cast<std::uint8_t>(edge.action.type));
        hash_integer(hash, static_cast<std::uint64_t>(edge.action.amount.units()));
        hash_integer(hash, static_cast<std::uint8_t>(edge.action.all_in_kind));
        hash_integer(hash, edge.action.requested_basis_points);
      } else {
        hash_integer(hash, edge.chance_card.value());
        hash_integer(hash, edge.physical_outcome_count);
        hash_integer(hash, edge.total_legal_outcome_count);
      }
    }
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

const char *tree_error_name(const TreeError error) noexcept {
  switch (error) {
  case TreeError::InvalidConfiguration:
    return "invalid_configuration";
  case TreeError::InvalidBoard:
    return "invalid_board";
  case TreeError::BuildLimitExceeded:
    return "build_limit_exceeded";
  case TreeError::NodeOverflow:
    return "node_overflow";
  case TreeError::GameFailure:
    return "game_failure";
  case TreeError::InvalidNode:
    return "invalid_node";
  case TreeError::NotChanceNode:
    return "not_chance_node";
  case TreeError::NotShowdownTerminal:
    return "not_showdown_terminal";
  case TreeError::InvalidPrivateCards:
    return "invalid_private_cards";
  case TreeError::EquityFailure:
    return "equity_failure";
  case TreeError::SettlementFailure:
    return "settlement_failure";
  case TreeError::StreamConsumerFailure:
    return "stream_consumer_failure";
  }
  return "unknown_tree_error";
}

} // namespace gtosd
