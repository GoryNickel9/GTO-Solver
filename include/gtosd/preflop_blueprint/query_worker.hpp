#pragma once

#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/traversal.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Postflop query worker of the chart viewer (roadmap P8.3, "decisioni
// postflop per board e path"): for a postflop entry, the action indices
// walked from it and the visible board, the 9 x 9 grid of hand classes with
// live combos, reach under the average strategy, action frequencies and the
// EV of every action in antes.
//
// EV per combo: counterfactual value of the action under the average
// strategy (opponent reach at the node inside) divided by the probability
// that the opponent reaches the node with a hand disjoint from the combo,
// i.e. the value of the action given the combo, the public history and the
// board, both players following the average strategy afterwards. River and
// turn nodes are exact (all remaining rivers enumerated); flop nodes average
// over sampled runouts (samplesPerAction of them, deterministic seed), so
// their EVs are estimates. Class rows aggregate the live combos weighted by
// the opponent reach; range frequencies weight combos by hero reach too.
namespace gtosd::preflop_blueprint {

struct QueryWorkerRequest {
  std::uint32_t entry_node{no_node};
  std::vector<std::uint32_t> action_indices;
  std::vector<CardId> board; // up to 5 cards, deal order
  std::uint32_t samples_per_action{64U};
  std::uint64_t seed{20260913ULL};
};

struct QueryWorkerAction {
  std::string id;
  double frequency{0.0};
  double ev_ante{0.0};
  double standard_error_ante{0.0};
};

struct QueryWorkerRow {
  std::string hand;
  std::uint32_t live_physical_combos{0U};
  bool reachable{false};
  double hero_reach{0.0};
  double strategy_ev_ante{0.0};
  std::vector<QueryWorkerAction> actions;
};

struct QueryWorkerResponse {
  std::uint32_t node{no_node};
  std::string node_id;
  Street street{Street::Flop};
  std::string player;
  std::uint32_t visible_board_cards{0U};
  bool exact{true};
  std::uint32_t runouts{0U};
  std::vector<std::string> action_ids;
  std::vector<double> action_frequencies; // range-level, per action
  std::array<QueryWorkerRow, 81> rows{};
  double seconds{0.0};
};

enum class QueryWorkerError : std::uint8_t {
  InvalidEntry,
  InvalidPath,
  MissingBoard,
  InvalidBoard,
  TerminalReached,
  EvaluationFailure
};

class QueryWorker {
public:
  QueryWorker(const CompiledGame &game, const BucketPolicy &policy,
              const BestResponseResources &resources, unsigned threads);

  [[nodiscard]] Result<QueryWorkerResponse, QueryWorkerError>
  query(const QueryWorkerRequest &request) const;

private:
  const CompiledGame *game_;
  const BucketPolicy *policy_;
  BestResponseResources resources_;
  unsigned threads_;
};

[[nodiscard]] std::string query_worker_response_json(const QueryWorkerResponse &response,
                                                     const CompiledGame &game,
                                                     const std::vector<CardId> &board);
[[nodiscard]] const char *query_worker_error_name(QueryWorkerError error) noexcept;

} // namespace gtosd::preflop_blueprint
