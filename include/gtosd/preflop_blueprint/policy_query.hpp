#pragma once

#include "gtosd/card_abstraction/bucket_tables.hpp"
#include "gtosd/card_abstraction/canonical_boards.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Query of a bucket policy (roadmap P8.2): given a legal action history, a
// combo and the board dealt so far, the action distribution of the policy at
// the reached decision node. The row is the hand class at the preflop and
// the street bucket of the combo on the board after: catalog lookup and
// bucket table, no feature is recomputed. Board cards are consumed at every
// street transition of the history (three for the flop, one for the turn and
// one for the river); a query that stops on a chance node consumes the cards
// of the next street and answers at the following decision node.
namespace gtosd::preflop_blueprint {

class HistoryBucketRows;

enum class QueryError : std::uint8_t {
  UnknownAction,
  IllegalHistory,
  MissingBoard,
  InvalidBoard,
  HandOnBoard,
  MissingTable,
  TerminalReached
};

struct QueryTables {
  const card_abstraction::BoardCatalog *catalog{nullptr};
  const card_abstraction::BucketTable *flop{nullptr};
  const card_abstraction::BucketTable *turn{nullptr};
  const card_abstraction::BucketTable *river{nullptr};
  const HistoryBucketRows *history_rows{nullptr};
};

struct QueryRequest {
  // Action ids as produced by action_labels.hpp, in play order.
  std::vector<std::string> actions;
  // Board cards in deal order: flop (any order), turn, river; only the cards
  // needed by the history are consumed.
  std::vector<CardId> board;
  std::array<CardId, 2> hand{};
};

struct QueryAction {
  std::string id;
  double probability{0.0};
};

struct QueryResult {
  std::uint32_t node{0U};
  std::string node_id;
  Street street{Street::Preflop};
  std::uint8_t actor{0U};
  std::uint32_t row{0U};
  std::uint32_t board_cards_used{0U};
  std::vector<QueryAction> actions;
};

[[nodiscard]] Result<QueryResult, QueryError> query_policy(const CompiledGame &game,
                                                           const BucketPolicy &policy,
                                                           const QueryTables &tables,
                                                           const QueryRequest &request);

// Row of a combo at a decision node for a board prefix: hand class at the
// preflop, street bucket after (used by the query and the export).
[[nodiscard]] Result<std::uint32_t, QueryError>
policy_row(const CompiledGame &game, const QueryTables &tables, std::uint32_t node,
           const std::array<CardId, 2> &hand, const std::vector<CardId> &board);

[[nodiscard]] const char *query_error_name(QueryError error) noexcept;

} // namespace gtosd::preflop_blueprint
