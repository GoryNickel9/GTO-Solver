#include "gtosd/preflop_blueprint/traversal.hpp"

#include <algorithm>
#include <cstring>

namespace gtosd::preflop_blueprint {
namespace {

constexpr double ante_scale = 1.0 / static_cast<double>(Money::units_per_ante);

bool all_zero(const double *values) noexcept {
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    if (values[hand] != 0.0) {
      return false;
    }
  }
  return true;
}

} // namespace

BucketPolicy::BucketPolicy(const CompiledGame &game, const StateLayout &layout)
    : game_(&game), layout_(layout), table_(layout.entries, 0.0) {
  set_uniform();
}

void BucketPolicy::set_uniform() {
  for (const auto &node : game_->nodes()) {
    if (node.kind != NodeKind::Decision) {
      continue;
    }
    const auto rows = StateLayout::rows_for(node.street, layout_.flop_capacity,
                                            layout_.turn_capacity, layout_.river_capacity);
    const auto probability = 1.0 / node.action_count;
    std::fill_n(table_.data() + layout_.offsets[node.id],
                static_cast<std::size_t>(rows) * node.action_count, probability);
  }
}

std::span<double> BucketPolicy::row(const std::uint32_t node,
                                    const std::uint32_t row_index) noexcept {
  const auto &entry = game_->nodes()[node];
  return std::span<double>(table_.data() + layout_.offsets[node] +
                               static_cast<std::size_t>(row_index) * entry.action_count,
                           entry.action_count);
}

std::span<const double> BucketPolicy::row(const std::uint32_t node,
                                          const std::uint32_t row_index) const noexcept {
  const auto &entry = game_->nodes()[node];
  return std::span<const double>(table_.data() + layout_.offsets[node] +
                                     static_cast<std::size_t>(row_index) * entry.action_count,
                                 entry.action_count);
}

const double *BucketPolicy::probabilities(const std::uint32_t node, const std::uint16_t hand,
                                          const BoardContext &context) const noexcept {
  const auto &entry = game_->nodes()[node];
  const auto row_index = context.row(entry.street, hand);
  const auto rows = StateLayout::rows_for(entry.street, layout_.flop_capacity,
                                          layout_.turn_capacity, layout_.river_capacity);
  if (row_index >= rows) {
    return nullptr;
  }
  return table_.data() + layout_.offsets[node] +
         static_cast<std::size_t>(row_index) * entry.action_count;
}

HandPolicy::HandPolicy(const CompiledGame &game) : game_(&game) {
  offsets_.assign(game.nodes().size(), no_offset);
  std::uint64_t total = 0U;
  for (const auto &node : game.nodes()) {
    if (node.kind != NodeKind::Decision) {
      continue;
    }
    offsets_[node.id] = total;
    total += static_cast<std::uint64_t>(live_hand_count) * node.action_count;
  }
  table_.assign(total, 0.0);
  set_uniform();
}

void HandPolicy::set_uniform() {
  for (const auto &node : game_->nodes()) {
    if (node.kind != NodeKind::Decision) {
      continue;
    }
    std::fill_n(table_.data() + offsets_[node.id], live_hand_count * node.action_count,
                1.0 / node.action_count);
  }
}

std::span<double> HandPolicy::row(const std::uint32_t node, const std::uint16_t hand) noexcept {
  const auto &entry = game_->nodes()[node];
  return std::span<double>(table_.data() + offsets_[node] +
                               static_cast<std::size_t>(hand) * entry.action_count,
                           entry.action_count);
}

const double *HandPolicy::probabilities(const std::uint32_t node, const std::uint16_t hand,
                                        const BoardContext &) const noexcept {
  const auto &entry = game_->nodes()[node];
  return table_.data() + offsets_[node] + static_cast<std::size_t>(hand) * entry.action_count;
}

ValueTraversal::ValueTraversal(const CompiledGame &game, const BoardContext &context,
                               const ShowdownKernel &kernel, const AllInEquityCache *all_in)
    : game_(&game), context_(&context), kernel_(&kernel), all_in_(all_in) {
  levels_.resize(static_cast<std::size_t>(game.stats().maximum_depth) + 2U);
  for (auto &level : levels_) {
    level.child_reach.assign(maximum_actions * live_hand_count, 0.0);
    level.child_values.assign(maximum_actions * live_hand_count, 0.0);
    level.scratch.assign(3U * live_hand_count, 0.0);
  }
}

Result<bool, KernelError> ValueTraversal::evaluate(const Policy &policy, const std::uint8_t hero,
                                                   const ConstHandSpan opponent_reach,
                                                   const HandSpan values,
                                                   const TraversalOptions &options) {
  return evaluate_from(game_->root(), policy, hero, opponent_reach, values, options);
}

Result<bool, KernelError> ValueTraversal::evaluate_from(const std::uint32_t node,
                                                        const Policy &policy,
                                                        const std::uint8_t hero,
                                                        const ConstHandSpan opponent_reach,
                                                        const HandSpan values,
                                                        const TraversalOptions &options) {
  using Outcome = Result<bool, KernelError>;
  if (node >= game_->nodes().size() || hero >= game_->config().player_count) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  policy_ = &policy;
  hero_ = hero;
  best_response_ = options.best_response;
  failed_ = false;
  counters_ = {};
  traverse(node, game_->nodes()[node].depth, opponent_reach.data(), values.data());
  if (failed_) {
    return Outcome::failure(KernelError::MissingTable);
  }
  return Outcome::success(true);
}

bool ValueTraversal::traverse(const std::uint32_t node_id, const std::uint32_t depth,
                              const double *opponent_reach, double *values) {
  ++counters_.nodes_visited;
  const auto &node = game_->nodes()[node_id];
  if (all_zero(opponent_reach)) {
    ++counters_.subtrees_pruned;
    std::fill_n(values, live_hand_count, 0.0);
    return true;
  }
  switch (node.kind) {
  case NodeKind::TerminalFold:
  case NodeKind::TerminalShowdown:
    terminal(node, opponent_reach, values, levels_[depth].scratch.data());
    return !failed_;
  case NodeKind::Chance:
    return traverse(game_->edges_of(node_id)[0].child, depth + 1U, opponent_reach, values);
  case NodeKind::Decision:
    break;
  }

  auto &level = levels_[depth];
  const auto edges = game_->edges_of(node_id);
  const auto actions = node.action_count;
  if (node.actor == hero_) {
    for (std::uint8_t action = 0; action < actions; ++action) {
      if (!traverse(edges[action].child, depth + 1U, opponent_reach,
                    level.child_values.data() + static_cast<std::size_t>(action) * live_hand_count)) {
        return false;
      }
    }
    if (best_response_) {
      for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
        double best = level.child_values[hand];
        for (std::uint8_t action = 1; action < actions; ++action) {
          best = std::max(best, level.child_values[static_cast<std::size_t>(action) * live_hand_count + hand]);
        }
        values[hand] = best;
      }
      return true;
    }
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      const double *probabilities =
          policy_->probabilities(node_id, static_cast<std::uint16_t>(hand), *context_);
      if (probabilities == nullptr) {
        failed_ = true;
        return false;
      }
      double value = 0.0;
      for (std::uint8_t action = 0; action < actions; ++action) {
        value += probabilities[action] *
                 level.child_values[static_cast<std::size_t>(action) * live_hand_count + hand];
      }
      values[hand] = value;
    }
    return true;
  }

  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const double reach = opponent_reach[hand];
    if (reach == 0.0) {
      for (std::uint8_t action = 0; action < actions; ++action) {
        level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] = 0.0;
      }
      continue;
    }
    const double *probabilities =
        policy_->probabilities(node_id, static_cast<std::uint16_t>(hand), *context_);
    if (probabilities == nullptr) {
      failed_ = true;
      return false;
    }
    for (std::uint8_t action = 0; action < actions; ++action) {
      level.child_reach[static_cast<std::size_t>(action) * live_hand_count + hand] =
          reach * probabilities[action];
    }
  }
  std::fill_n(values, live_hand_count, 0.0);
  for (std::uint8_t action = 0; action < actions; ++action) {
    if (!traverse(edges[action].child, depth + 1U,
                  level.child_reach.data() + static_cast<std::size_t>(action) * live_hand_count,
                  level.child_values.data())) {
      return false;
    }
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      values[hand] += level.child_values[hand];
    }
  }
  return true;
}

void ValueTraversal::terminal(const CompiledNode &node, const double *opponent_reach,
                              double *values, double *scratch) {
  ++counters_.terminals_evaluated;
  const ConstHandSpan reach(opponent_reach, live_hand_count);
  const HandSpan first(scratch, live_hand_count);
  const HandSpan second(scratch + live_hand_count, live_hand_count);
  const HandSpan third(scratch + 2U * live_hand_count, live_hand_count);
  if (node.kind == NodeKind::TerminalFold) {
    fold_mass(*context_, reach, first);
    const double payoff = static_cast<double>(game_->fold_payoffs(node.id)[hero_]) * ante_scale;
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      values[hand] = payoff * first[hand];
    }
    return;
  }
  if (node.street == Street::Preflop) {
    if (all_in_ == nullptr) {
      failed_ = true;
      std::fill_n(values, live_hand_count, 0.0);
      return;
    }
    all_in_->masses(*context_, reach, first, second, third);
  } else {
    const std::array<ConstHandSpan, maximum_players> reach_by_player{
        reach, reach, reach, reach, reach, reach};
    kernel_->evaluate(*context_, node.active_mask, hero_,
                      std::span<const ConstHandSpan>(reach_by_player.data(),
                                                     game_->config().player_count),
                      first, second, third);
  }
  const auto hero_bit = static_cast<std::uint8_t>(std::uint8_t{1} << hero_);
  const auto opponents = static_cast<std::uint8_t>(node.active_mask & ~hero_bit);
  const double win = static_cast<double>(game_->showdown_payoffs(node.id, hero_bit)[hero_]) * ante_scale;
  const double tie =
      static_cast<double>(game_->showdown_payoffs(node.id, node.active_mask)[hero_]) * ante_scale;
  const double lose = static_cast<double>(game_->showdown_payoffs(node.id, opponents)[hero_]) * ante_scale;
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    values[hand] = win * first[hand] + tie * second[hand] + lose * third[hand];
  }
}

} // namespace gtosd::preflop_blueprint
