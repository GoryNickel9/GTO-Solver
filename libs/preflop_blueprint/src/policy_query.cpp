#include "gtosd/preflop_blueprint/policy_query.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"

#include <algorithm>

namespace gtosd::preflop_blueprint {
namespace {

namespace ca = card_abstraction;

std::uint32_t board_cards_for(const Street street) noexcept {
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
  return 0U;
}

} // namespace

Result<std::uint32_t, QueryError> policy_row(const CompiledGame &game, const QueryTables &tables,
                                              const std::uint32_t node,
                                              const std::array<CardId, 2> &hand,
                                              const std::vector<CardId> &board) {
  using Outcome = Result<std::uint32_t, QueryError>;
  if (hand[0] == hand[1]) {
    return Outcome::failure(QueryError::InvalidBoard);
  }
  const auto &entry = game.nodes()[node];
  const auto needed = board_cards_for(entry.street);
  if (board.size() < needed) {
    return Outcome::failure(QueryError::MissingBoard);
  }
  for (std::uint32_t index = 0; index < needed; ++index) {
    if (board[index] == hand[0] || board[index] == hand[1]) {
      return Outcome::failure(QueryError::HandOnBoard);
    }
    for (std::uint32_t other = index + 1U; other < needed; ++other) {
      if (board[index] == board[other]) {
        return Outcome::failure(QueryError::InvalidBoard);
      }
    }
  }
  const auto combo = ca::combo_index(hand[0], hand[1]);
  if (entry.street == Street::Preflop) {
    return Outcome::success(static_cast<std::uint32_t>(ca::combo_table().hand_class[combo]));
  }
  if (tables.catalog == nullptr) {
    return Outcome::failure(QueryError::MissingTable);
  }
  std::array<CardId, 3> flop{board[0], board[1], board[2]};
  std::sort(flop.begin(), flop.end());
  std::array<std::uint16_t, 3> buckets{ca::no_bucket, ca::no_bucket, ca::no_bucket};
  const auto assign_bucket = [&](const std::size_t index, const ca::BucketTable *table,
                                 const Result<ca::CanonicalLookup, CardError> &lookup)
      -> Result<bool, QueryError> {
    if (table == nullptr) {
      return Result<bool, QueryError>::failure(QueryError::MissingTable);
    }
    if (!lookup) {
      return Result<bool, QueryError>::failure(QueryError::InvalidBoard);
    }
    const auto &permutation = lookup.value().permutation;
    const auto first = ca::permute_card(hand[0], permutation);
    const auto second = ca::permute_card(hand[1], permutation);
    buckets[index] = table->bucket(lookup.value().index, ca::combo_index(first, second));
    if (buckets[index] == ca::no_bucket) {
      return Result<bool, QueryError>::failure(QueryError::MissingTable);
    }
    return Result<bool, QueryError>::success(true);
  };
  auto assigned = assign_bucket(0U, tables.flop, tables.catalog->lookup_flop(flop));
  if (!assigned) {
    return Outcome::failure(assigned.error());
  }
  if (entry.street == Street::Turn || entry.street == Street::River) {
    assigned = assign_bucket(1U, tables.turn, tables.catalog->lookup_flop_turn(flop, board[3]));
    if (!assigned) {
      return Outcome::failure(assigned.error());
    }
  }
  if (entry.street == Street::River) {
    const std::array<CardId, 5> cards{flop[0], flop[1], flop[2], board[3], board[4]};
    assigned = assign_bucket(2U, tables.river, tables.catalog->lookup_river_board(cards));
    if (!assigned) {
      return Outcome::failure(assigned.error());
    }
  }
  if (tables.history_rows != nullptr) {
    if (tables.flop == nullptr || tables.turn == nullptr || tables.river == nullptr ||
        !tables.history_rows->matches(*tables.flop) || !tables.history_rows->matches(*tables.turn) ||
        !tables.history_rows->matches(*tables.river)) {
      return Outcome::failure(QueryError::MissingTable);
    }
    const auto mapped = tables.history_rows->row(
        entry.street, ca::combo_table().hand_class[combo], buckets[0], buckets[1], buckets[2]);
    if (mapped == no_history_row) {
      return Outcome::failure(QueryError::MissingTable);
    }
    return Outcome::success(mapped);
  }
  return Outcome::success(
      static_cast<std::uint32_t>(buckets[static_cast<std::size_t>(entry.street) - 1U]));
}

Result<QueryResult, QueryError> query_policy(const CompiledGame &game, const BucketPolicy &policy,
                                             const QueryTables &tables,
                                             const QueryRequest &request) {
  using Outcome = Result<QueryResult, QueryError>;
  auto node = game.root();
  std::uint32_t cards_used = 0U;
  // Enters the child of a chance node after checking that the board holds
  // the cards of the next street.
  const auto pass_chance = [&](std::uint32_t &current) -> bool {
    while (game.nodes()[current].kind == NodeKind::Chance) {
      const auto child = game.edges_of(current)[0].child;
      const auto needed = board_cards_for(game.nodes()[child].street);
      if (request.board.size() < needed) {
        return false;
      }
      cards_used = needed;
      current = child;
    }
    return true;
  };
  for (const auto &action : request.actions) {
    const auto &entry = game.nodes()[node];
    if (entry.kind != NodeKind::Decision) {
      return Outcome::failure(entry.kind == NodeKind::Chance ? QueryError::MissingBoard
                                                             : QueryError::TerminalReached);
    }
    const auto labels = edge_labels(game, node);
    const auto found = std::find(labels.begin(), labels.end(), action);
    if (found == labels.end()) {
      return Outcome::failure(QueryError::UnknownAction);
    }
    node = game.edges_of(node)[static_cast<std::size_t>(found - labels.begin())].child;
    if (!pass_chance(node)) {
      return Outcome::failure(QueryError::MissingBoard);
    }
  }
  if (!pass_chance(node)) {
    return Outcome::failure(QueryError::MissingBoard);
  }
  const auto &entry = game.nodes()[node];
  if (entry.kind != NodeKind::Decision) {
    return Outcome::failure(QueryError::TerminalReached);
  }
  const auto row = policy_row(game, tables, node, request.hand, request.board);
  if (!row) {
    return Outcome::failure(row.error());
  }
  const auto rows = StateLayout::rows_for(entry.street, policy.layout().flop_capacity,
                                          policy.layout().turn_capacity,
                                          policy.layout().river_capacity);
  if (row.value() >= rows) {
    return Outcome::failure(QueryError::MissingTable);
  }
  QueryResult result;
  result.node = node;
  result.node_id = node_path_id(game, node);
  result.street = entry.street;
  result.actor = entry.actor;
  result.row = row.value();
  result.board_cards_used = cards_used;
  const auto labels = edge_labels(game, node);
  const auto probabilities = policy.row(node, row.value());
  for (std::size_t index = 0; index < labels.size(); ++index) {
    result.actions.push_back({labels[index], probabilities[index]});
  }
  return Outcome::success(std::move(result));
}

const char *query_error_name(const QueryError error) noexcept {
  switch (error) {
  case QueryError::UnknownAction:
    return "unknown_action";
  case QueryError::IllegalHistory:
    return "illegal_history";
  case QueryError::MissingBoard:
    return "missing_board";
  case QueryError::InvalidBoard:
    return "invalid_board";
  case QueryError::HandOnBoard:
    return "hand_on_board";
  case QueryError::MissingTable:
    return "missing_table";
  case QueryError::TerminalReached:
    return "terminal_reached";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
