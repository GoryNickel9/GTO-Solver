#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "hashing.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <string>
#include <utility>

namespace gtosd::preflop_blueprint {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint8_t dealt_board_cards(const Street street) noexcept {
  switch (street) {
  case Street::Preflop:
    return 0U;
  case Street::Flop:
    return 3U;
  case Street::Turn:
    return 4U;
  case Street::River:
    return 5U;
  }
  return 5U;
}

std::string edge_text(const Action &action) {
  return std::to_string(static_cast<unsigned>(action.type)) + ":" +
         std::to_string(action.amount.units()) + ";";
}

} // namespace

class GameCompiler {
public:
  GameCompiler(const GameConfig &config, const CompileOptions &options, CompiledGame &game)
      : config_(config), options_(options), game_(game) {}

  Result<bool, GameModelError> run(const PublicState *subgame_root) {
    using Outcome = Result<bool, GameModelError>;
    const auto started = Clock::now();
    PublicState root_state;
    if (subgame_root != nullptr) {
      if (subgame_root->status != HandStatus::InProgress ||
          subgame_root->player_count != config_.player_count || !validate_state(*subgame_root)) {
        return Outcome::failure(GameModelError::InvalidConfiguration);
      }
      root_state = *subgame_root;
      subgame_ = true;
    } else {
      const auto root = make_preflop_state(config_);
      if (!root) {
        return Outcome::failure(root.error());
      }
      root_state = root.value();
    }
    const auto root_id = expand(root_state, 0U, 0U, no_node, no_entry);
    if (!root_id) {
      return Outcome::failure(root_id.error());
    }
    game_.stats_.node_count = game_.nodes_.size();
    game_.stats_.edge_count = game_.edges_.size();
    game_.stats_.postflop_entries = game_.entries_.size();
    game_.stats_.compile_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    game_.fingerprint_ = fingerprint();
    return Outcome::success(true);
  }

private:
  using NodeResult = Result<std::uint32_t, GameModelError>;

  NodeResult expand(const PublicState &state, const AggressionLevel level,
                    const std::uint16_t depth, const std::uint32_t parent,
                    std::uint16_t entry, const bool limped_pot = false) {
    if (game_.nodes_.size() >= options_.maximum_nodes || game_.nodes_.size() >= no_node) {
      return NodeResult::failure(GameModelError::NodeOverflow);
    }
    const auto id = static_cast<std::uint32_t>(game_.nodes_.size());
    CompiledNode node;
    node.id = id;
    node.parent = parent;
    node.depth = depth;
    node.street = state.street;
    node.active_mask = state.active_players_mask;
    node.level = level;
    node.limped_pot = limped_pot;
    switch (state.status) {
    case HandStatus::Folded:
      node.kind = NodeKind::TerminalFold;
      break;
    case HandStatus::Showdown:
      node.kind = NodeKind::TerminalShowdown;
      node.remaining_board_cards = 0U;
      break;
    case HandStatus::AllInRunout:
      node.kind = NodeKind::TerminalShowdown;
      node.remaining_board_cards = static_cast<std::uint8_t>(5U - dealt_board_cards(state.street));
      break;
    case HandStatus::StreetComplete:
      node.kind = NodeKind::Chance;
      break;
    case HandStatus::InProgress:
      node.kind = NodeKind::Decision;
      node.actor = state.player_to_act;
      break;
    }
    if (options_.checkdown_at_flop && node.kind == NodeKind::Chance &&
        state.street == Street::Preflop) {
      node.kind = NodeKind::TerminalShowdown;
      node.remaining_board_cards = 5U;
    }
    if (node.kind == NodeKind::Chance && state.street == Street::Preflop) {
      if (game_.entries_.size() >= no_entry) {
        return NodeResult::failure(GameModelError::NodeOverflow);
      }
      entry = static_cast<std::uint16_t>(game_.entries_.size());
      game_.entries_.push_back(id);
    }
    node.postflop_entry = entry;
    game_.nodes_.push_back(node);
    game_.states_.push_back(state);
    account(node, state);

    std::vector<CompiledEdge> edges;
    switch (node.kind) {
    case NodeKind::TerminalFold:
    case NodeKind::TerminalShowdown: {
      // A checkdown leaf keeps its StreetComplete state; it settles as a
      // showdown of the players still in the hand.
      auto settled_state = state;
      if (settled_state.status == HandStatus::StreetComplete) {
        settled_state.status = HandStatus::Showdown;
      }
      const auto settled = settle(id, settled_state);
      if (!settled) {
        return NodeResult::failure(settled.error());
      }
      break;
    }
    case NodeKind::Chance: {
      if (state.street == Street::Preflop && options_.preflop_only) {
        break;
      }
      const auto next = advance_to_next_street(state);
      if (!next) {
        return NodeResult::failure(next.error());
      }
      const auto child = expand(next.value(), 0U, static_cast<std::uint16_t>(depth + 1U), id, entry);
      if (!child) {
        return NodeResult::failure(child.error());
      }
      edges.push_back(CompiledEdge{Action{}, child.value()});
      break;
    }
    case NodeKind::Decision: {
      const auto action_config = action_config_at(config_, state, level, limped_pot);
      if (!action_config) {
        return NodeResult::failure(action_config.error());
      }
      const auto actions = legal_actions(state, action_config.value());
      if (!actions || actions.value().empty() || actions.value().size() > maximum_actions) {
        return NodeResult::failure(GameModelError::GameFailure);
      }
      edges.reserve(actions.value().size());
      for (const auto &action : actions.value()) {
        const auto child_state = apply_action_at(state, action, action_config.value());
        if (!child_state) {
          return NodeResult::failure(child_state.error());
        }
        const auto child_level =
            static_cast<AggressionLevel>(level + (is_aggressive(action) ? 1U : 0U));
        // A call at preflop level 0 is a limp: it does not raise the level, but
        // it does change which response list the next raise answers to. The
        // flag never leaves the preflop street.
        const auto child_limped =
            (state.street == Street::Preflop) &&
            (limped_pot || (level == 0U && action.type == ActionType::Call));
        const auto child = expand(child_state.value(), child_level,
                                  static_cast<std::uint16_t>(depth + 1U), id, entry,
                                  child_limped);
        if (!child) {
          return NodeResult::failure(child.error());
        }
        edges.push_back(CompiledEdge{action, child.value()});
      }
      break;
    }
    }

    auto &stored = game_.nodes_[id];
    stored.first_edge = static_cast<std::uint32_t>(game_.edges_.size());
    stored.action_count = static_cast<std::uint8_t>(edges.size());
    stored.subtree_end = static_cast<std::uint32_t>(game_.nodes_.size());
    game_.edges_.insert(game_.edges_.end(), edges.begin(), edges.end());
    if (entry != no_entry && node.kind == NodeKind::Decision) {
      game_.stats_.postflop_action_edges += edges.size();
    }
    return NodeResult::success(id);
  }

  void account(const CompiledNode &node, const PublicState &state) {
    auto &stats = game_.stats_;
    stats.maximum_depth = std::max<std::uint32_t>(stats.maximum_depth, node.depth);
    stats.maximum_raise_count = std::max(stats.maximum_raise_count, state.raise_count_this_street);
    switch (node.kind) {
    case NodeKind::Decision:
      ++stats.decision_nodes;
      break;
    case NodeKind::Chance:
      ++stats.chance_nodes;
      break;
    case NodeKind::TerminalFold:
      ++stats.terminal_folds;
      break;
    case NodeKind::TerminalShowdown:
      ++stats.terminal_showdowns;
      break;
    }
    if (state.street == Street::Preflop) {
      ++stats.preflop_nodes;
      if (node.kind == NodeKind::Decision) {
        ++stats.preflop_decisions;
      } else if (node.kind == NodeKind::TerminalFold) {
        ++stats.preflop_terminal_folds;
      } else if (node.kind == NodeKind::TerminalShowdown) {
        ++stats.preflop_all_in_runouts;
      }
    }
    if (node.postflop_entry != no_entry) {
      ++stats.postflop_represented_nodes;
      switch (node.kind) {
      case NodeKind::Decision:
        ++stats.postflop_decisions;
        ++stats.postflop_decisions_by_street[static_cast<std::size_t>(state.street) - 1U];
        break;
      case NodeKind::Chance:
        ++stats.postflop_chance_frontiers;
        break;
      case NodeKind::TerminalFold:
        ++stats.postflop_terminal_folds;
        break;
      case NodeKind::TerminalShowdown:
        if (node.remaining_board_cards == 0U) {
          ++stats.postflop_terminal_showdowns;
        } else {
          ++stats.postflop_terminal_all_in_runouts;
        }
        break;
      }
    }
  }

  Result<bool, GameModelError> settle(const std::uint32_t id, const PublicState &state) {
    using Outcome = Result<bool, GameModelError>;
    auto &node = game_.nodes_[id];
    node.payoff_offset = static_cast<std::uint32_t>(game_.payoffs_.size());
    const auto push = [&](const Settlement &settlement) {
      for (std::uint8_t player = 0; player < state.player_count; ++player) {
        game_.payoffs_.push_back(settlement.payoff_units[player]);
      }
    };
    if (node.kind == NodeKind::TerminalFold) {
      const auto settlement = settle_terminal(state, config_.rake);
      if (!settlement) {
        return Outcome::failure(GameModelError::GameFailure);
      }
      push(settlement.value());
      return Outcome::success(true);
    }
    for (std::uint8_t winners = 1U; winners <= state.active_players_mask; ++winners) {
      if ((winners & static_cast<std::uint8_t>(~state.active_players_mask)) != 0U) {
        continue;
      }
      const auto settlement = settle_terminal(state, config_.rake, winners);
      if (!settlement) {
        return Outcome::failure(GameModelError::GameFailure);
      }
      push(settlement.value());
    }
    return Outcome::success(true);
  }

  std::string fingerprint() const {
    auto hash = detail::fnv1a_text("gtosd.preflop_blueprint_game_tree.v1|");
    hash = detail::fnv1a_text(game_config_fingerprint(config_), hash);
    hash = detail::fnv1a_text(subgame_                     ? "|subgame|"
                              : options_.checkdown_at_flop ? "|checkdown|"
                              : options_.preflop_only      ? "|preflop_only|"
                                                           : "|full|",
                              hash);
    for (const auto &node : game_.nodes_) {
      hash = detail::fnv1a_text(std::to_string(static_cast<unsigned>(node.kind)) + ":" +
                                    std::to_string(static_cast<unsigned>(node.level)) + ":" +
                                    serialize_public_state(game_.states_[node.id]) + ";",
                                hash);
      for (const auto &edge : game_.edges_of(node.id)) {
        hash = detail::fnv1a_text(edge_text(edge.action), hash);
      }
    }
    return "fnv1a64:" + detail::hex64_text(hash);
  }

  const GameConfig &config_;
  const CompileOptions &options_;
  CompiledGame &game_;
  bool subgame_{false};
};

std::span<const std::int64_t> CompiledGame::fold_payoffs(const std::uint32_t node) const noexcept {
  const auto &entry = nodes_[node];
  return std::span<const std::int64_t>(payoffs_.data() + entry.payoff_offset,
                                       config_.player_count);
}

std::span<const std::int64_t>
CompiledGame::showdown_payoffs(const std::uint32_t node,
                               const std::uint8_t winner_mask) const noexcept {
  const auto &entry = nodes_[node];
  const auto row = showdown_row(entry.active_mask, winner_mask);
  return std::span<const std::int64_t>(payoffs_.data() + entry.payoff_offset +
                                           static_cast<std::size_t>(row) * config_.player_count,
                                       config_.player_count);
}

std::uint32_t CompiledGame::showdown_rows(const std::uint8_t active_mask) noexcept {
  return (std::uint32_t{1} << std::popcount(active_mask)) - 1U;
}

std::uint32_t CompiledGame::showdown_row(const std::uint8_t active_mask,
                                         const std::uint8_t winner_mask) noexcept {
  std::uint32_t row = 0U;
  for (std::uint8_t mask = 1U; mask <= active_mask; ++mask) {
    if ((mask & static_cast<std::uint8_t>(~active_mask)) != 0U) {
      continue;
    }
    if (mask == winner_mask) {
      return row;
    }
    ++row;
  }
  return no_node;
}

Result<CompiledGame, GameModelError> CompiledGame::compile(const GameConfig &config,
                                                           const CompileOptions &options) {
  using Outcome = Result<CompiledGame, GameModelError>;
  if (!validate_game_config(config)) {
    return Outcome::failure(GameModelError::InvalidConfiguration);
  }
  CompiledGame game;
  game.config_ = config;
  GameCompiler compiler(config, options, game);
  const auto outcome = compiler.run(nullptr);
  if (!outcome) {
    return Outcome::failure(outcome.error());
  }
  return Outcome::success(std::move(game));
}

Result<CompiledGame, GameModelError> CompiledGame::compile_subgame(const GameConfig &config,
                                                                   const PublicState &root,
                                                                   const CompileOptions &options) {
  using Outcome = Result<CompiledGame, GameModelError>;
  if (!validate_game_config(config)) {
    return Outcome::failure(GameModelError::InvalidConfiguration);
  }
  CompiledGame game;
  game.config_ = config;
  GameCompiler compiler(config, options, game);
  const auto outcome = compiler.run(&root);
  if (!outcome) {
    return Outcome::failure(outcome.error());
  }
  return Outcome::success(std::move(game));
}

std::uint32_t StateLayout::rows_for(const Street street, const std::uint32_t flop,
                                    const std::uint32_t turn, const std::uint32_t river) noexcept {
  switch (street) {
  case Street::Preflop:
    return static_cast<std::uint32_t>(card_abstraction::preflop_hand_classes);
  case Street::Flop:
    return flop;
  case Street::Turn:
    return turn;
  case Street::River:
    return river;
  }
  return 0U;
}

StateLayout layout_state(const CompiledGame &game, const std::uint32_t flop_capacity,
                         const std::uint32_t turn_capacity, const std::uint32_t river_capacity) {
  StateLayout layout;
  layout.flop_capacity = flop_capacity;
  layout.turn_capacity = turn_capacity;
  layout.river_capacity = river_capacity;
  layout.offsets.assign(game.nodes().size(), no_offset);
  for (const auto &node : game.nodes()) {
    if (node.kind != NodeKind::Decision) {
      continue;
    }
    const auto rows =
        StateLayout::rows_for(node.street, flop_capacity, turn_capacity, river_capacity);
    const auto span = static_cast<std::uint64_t>(rows) * node.action_count;
    layout.offsets[node.id] = layout.entries;
    layout.entries += span;
    layout.entries_by_street[static_cast<std::size_t>(node.street)] += span;
  }
  return layout;
}

const char *node_kind_name(const NodeKind kind) noexcept {
  switch (kind) {
  case NodeKind::Decision:
    return "decision";
  case NodeKind::Chance:
    return "chance";
  case NodeKind::TerminalFold:
    return "terminal_fold";
  case NodeKind::TerminalShowdown:
    return "terminal_showdown";
  }
  return "unknown";
}

const char *street_name(const Street street) noexcept {
  switch (street) {
  case Street::Preflop:
    return "preflop";
  case Street::Flop:
    return "flop";
  case Street::Turn:
    return "turn";
  case Street::River:
    return "river";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
