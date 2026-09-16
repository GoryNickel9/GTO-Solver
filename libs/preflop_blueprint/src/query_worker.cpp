#include "gtosd/preflop_blueprint/query_worker.hpp"

#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/board_context.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"
#include "gtosd/preflop_blueprint/policy_query.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gtosd::preflop_blueprint {
namespace {

namespace ca = card_abstraction;
using Clock = std::chrono::steady_clock;
constexpr std::size_t combo_total = 630U;

std::array<CardId, 2> table_hand(const std::uint16_t combo) {
  const auto cards = ca::combo_table().cards[combo];
  return {CardId::from_index(cards[0]).value(), CardId::from_index(cards[1]).value()};
}

std::uint32_t visible_cards(const Street street) noexcept {
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

QueryWorker::QueryWorker(const CompiledGame &game, const BucketPolicy &policy,
                         const BestResponseResources &resources, const unsigned threads)
    : game_(&game), policy_(&policy), resources_(resources), threads_(threads) {}

Result<QueryWorkerResponse, QueryWorkerError>
QueryWorker::query(const QueryWorkerRequest &request) const {
  using Outcome = Result<QueryWorkerResponse, QueryWorkerError>;
  const auto started = Clock::now();
  const auto &game = *game_;
  const auto &nodes = game.nodes();
  const auto entries = game.postflop_entries();
  if (request.entry_node >= nodes.size() ||
      std::find(entries.begin(), entries.end(), request.entry_node) == entries.end()) {
    return Outcome::failure(QueryWorkerError::InvalidEntry);
  }
  // Resolve the node.
  auto node = game.edges_of(request.entry_node)[0].child;
  for (const auto index : request.action_indices) {
    const auto &entry = nodes[node];
    if (entry.kind != NodeKind::Decision) {
      return Outcome::failure(QueryWorkerError::TerminalReached);
    }
    if (index >= entry.action_count) {
      return Outcome::failure(QueryWorkerError::InvalidPath);
    }
    node = game.edges_of(node)[index].child;
    while (nodes[node].kind == NodeKind::Chance) {
      node = game.edges_of(node)[0].child;
    }
  }
  const auto &target = nodes[node];
  if (target.kind != NodeKind::Decision) {
    return Outcome::failure(QueryWorkerError::TerminalReached);
  }
  const auto visible = visible_cards(target.street);
  if (request.board.size() < visible) {
    return Outcome::failure(QueryWorkerError::MissingBoard);
  }
  for (std::uint32_t index = 0; index < visible; ++index) {
    for (std::uint32_t other = index + 1U; other < visible; ++other) {
      if (request.board[index] == request.board[other]) {
        return Outcome::failure(QueryWorkerError::InvalidBoard);
      }
    }
  }
  std::vector<CardId> board(request.board.begin(), request.board.begin() + visible);
  std::array<CardId, 3> flop{board[0], board[1], board[2]};
  std::sort(flop.begin(), flop.end());
  std::uint64_t board_mask = 0U;
  for (const auto card : board) {
    board_mask |= card.mask();
  }
  const auto hero = target.actor;
  const auto opponent = static_cast<std::uint8_t>(1U - hero);
  const auto &policy = *policy_;
  const auto &table = ca::combo_table();
  QueryTables tables;
  tables.catalog = resources_.catalog;
  tables.flop = resources_.flop;
  tables.turn = resources_.turn;
  tables.river = resources_.river;

  // Hero reach per combo along the path (rows on the visible board).
  std::vector<double> hero_reach(combo_total, 1.0);
  std::vector<std::uint8_t> live(combo_total, 0U);
  for (std::uint16_t combo = 0; combo < combo_total; ++combo) {
    live[combo] = (table.masks[combo] & board_mask) == 0U ? 1U : 0U;
  }
  {
    auto current = game.root();
    for (const auto edge : path_edges(game, node)) {
      const auto &entry = nodes[current];
      if (entry.kind == NodeKind::Decision && entry.actor == hero) {
        for (std::uint16_t combo = 0; combo < combo_total; ++combo) {
          if (live[combo] == 0U || hero_reach[combo] == 0.0) {
            continue;
          }
          const auto row = policy_row(game, tables, current, table_hand(combo), board);
          if (!row) {
            hero_reach[combo] = 0.0;
            continue;
          }
          hero_reach[combo] *= policy.row(current, row.value())[edge];
        }
      }
      current = game.edges_of(current)[edge].child;
    }
  }

  // Action values per combo and opponent mass at the node.
  const auto action_count = static_cast<std::size_t>(target.action_count);
  std::vector<std::vector<double>> action_values(action_count, std::vector<double>(combo_total, 0.0));
  std::vector<double> opponent_mass(combo_total, 0.0);
  QueryWorkerResponse response;
  response.exact = true;
  response.runouts = 0U;
  auto evaluator = BestResponseEvaluator::create(game, policy, resources_);
  if (!evaluator) {
    return Outcome::failure(QueryWorkerError::EvaluationFailure);
  }
  if (target.street == Street::River) {
    // Reach at the turn->river chance ancestor, then through the river street.
    auto chance = target.parent;
    while (chance != no_node && nodes[chance].kind != NodeKind::Chance) {
      chance = nodes[chance].parent;
    }
    if (chance == no_node) {
      return Outcome::failure(QueryWorkerError::EvaluationFailure);
    }
    FlopGroup group;
    group.flop = flop;
    WeightedBoard single;
    single.history.flop = flop;
    single.history.turn = board[3];
    single.history.river = board[4];
    group.boards.push_back(single);
    auto probe = evaluator.value().probe_node(group, chance, hero);
    if (!probe || !probe.value().found) {
      return Outcome::failure(QueryWorkerError::EvaluationFailure);
    }
    AbstractionTables abstraction;
    abstraction.catalog = resources_.catalog;
    abstraction.flop = resources_.flop;
    abstraction.turn = resources_.turn;
    abstraction.river = resources_.river;
    const auto context = BoardContext::build(single.history, *resources_.ranks, &abstraction);
    if (!context) {
      return Outcome::failure(QueryWorkerError::EvaluationFailure);
    }
    const auto combos = context.value().combo_ids();
    std::array<double, live_hand_count> reach{};
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      reach[hand] = probe.value().opponent_reach[combos[hand]];
    }
    // River street: from the chance child down to the node.
    auto current = game.edges_of(chance)[0].child;
    const auto path = path_edges(game, node);
    const auto chance_depth = path_edges(game, current).size();
    for (std::size_t step = chance_depth; step < path.size(); ++step) {
      const auto &entry = nodes[current];
      if (entry.kind == NodeKind::Decision && entry.actor == opponent) {
        for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
          if (reach[hand] != 0.0) {
            reach[hand] *= policy.row(current, context.value().row(Street::River, static_cast<std::uint16_t>(hand)))[path[step]];
          }
        }
      }
      current = game.edges_of(current)[path[step]].child;
    }
    std::array<double, live_hand_count> disjoint{};
    fold_mass(context.value(), reach, disjoint);
    const HeadsUpShowdownKernel kernel;
    ValueTraversal traversal(game, context.value(), kernel, nullptr);
    std::array<double, live_hand_count> values{};
    const auto edges = game.edges_of(node);
    for (std::size_t action = 0; action < action_count; ++action) {
      TraversalOptions options;
      if (!traversal.evaluate_from(edges[action].child, policy, hero, reach, values, options)) {
        return Outcome::failure(QueryWorkerError::EvaluationFailure);
      }
      for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
        action_values[action][combos[hand]] = values[hand];
      }
    }
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      opponent_mass[combos[hand]] = disjoint[hand];
    }
  } else {
    FlopGroup group;
    group.flop = flop;
    if (target.street == Street::Turn) {
      for (std::uint8_t river = 0; river < 36U; ++river) {
        const auto card = CardId::from_index(river).value();
        if ((board_mask & card.mask()) != 0U) {
          continue;
        }
        WeightedBoard entry;
        entry.history.flop = flop;
        entry.history.turn = board[3];
        entry.history.river = card;
        group.boards.push_back(entry);
      }
    } else {
      auto all = full_runouts(flop);
      const auto wanted = std::max<std::uint32_t>(1U, request.samples_per_action);
      if (wanted >= all.boards.size()) {
        group.boards = all.boards;
      } else {
        ca::DeterministicRandom random(request.seed);
        for (std::size_t index = 0; index < wanted; ++index) {
          const auto pick = index + static_cast<std::size_t>(random.uniform_unit() *
                                                              static_cast<double>(all.boards.size() - index));
          std::swap(all.boards[index], all.boards[std::min(pick, all.boards.size() - 1U)]);
        }
        group.boards.assign(all.boards.begin(), all.boards.begin() + wanted);
        response.exact = false;
      }
    }
    response.runouts = static_cast<std::uint32_t>(group.boards.size());
    auto probe = evaluator.value().probe_node(group, node, hero);
    if (!probe || !probe.value().found || probe.value().action_values.size() != action_count) {
      return Outcome::failure(QueryWorkerError::EvaluationFailure);
    }
    action_values = probe.value().action_values;
    opponent_mass = probe.value().opponent_mass;
  }

  // Rows per class.
  response.node = node;
  response.node_id = node_path_id(game, node);
  response.street = target.street;
  response.player = position_name(game, hero);
  response.visible_board_cards = visible;
  response.action_ids = edge_labels(game, node);
  response.action_frequencies.assign(action_count, 0.0);
  double range_weight = 0.0;
  std::array<std::vector<std::uint16_t>, 81> members{};
  for (std::uint16_t combo = 0; combo < combo_total; ++combo) {
    members[table.hand_class[combo]].push_back(combo);
  }
  for (std::uint8_t hand_class = 0; hand_class < 81U; ++hand_class) {
    auto &row = response.rows[hand_class];
    row.hand = class_name(hand_class);
    row.actions.resize(action_count);
    for (std::size_t action = 0; action < action_count; ++action) {
      row.actions[action].id = response.action_ids[action];
    }
    double reach_total = 0.0;
    double ev_weight = 0.0;
    std::vector<double> frequency(action_count, 0.0);
    std::vector<double> ev(action_count, 0.0);
    double strategy_ev = 0.0;
    std::uint32_t live_count = 0U;
    for (const auto combo : members[hand_class]) {
      if (live[combo] == 0U) {
        continue;
      }
      ++live_count;
      const auto row_index = policy_row(game, tables, node, table_hand(combo), board);
      if (!row_index) {
        continue;
      }
      const auto probabilities = policy.row(node, row_index.value());
      const double mass = opponent_mass[combo];
      reach_total += hero_reach[combo];
      double combo_strategy_ev = 0.0;
      for (std::size_t action = 0; action < action_count; ++action) {
        frequency[action] += probabilities[action];
        if (mass > 0.0) {
          const double value = action_values[action][combo] / mass;
          ev[action] += mass * value;
          combo_strategy_ev += probabilities[action] * value;
        }
        response.action_frequencies[action] += hero_reach[combo] * mass * probabilities[action];
      }
      range_weight += hero_reach[combo] * mass;
      if (mass > 0.0) {
        ev_weight += mass;
        strategy_ev += mass * combo_strategy_ev;
      }
    }
    row.live_physical_combos = live_count;
    if (live_count == 0U) {
      continue;
    }
    row.hero_reach = reach_total / static_cast<double>(live_count);
    row.reachable = row.hero_reach > 1e-12;
    for (std::size_t action = 0; action < action_count; ++action) {
      row.actions[action].frequency = frequency[action] / static_cast<double>(live_count);
      row.actions[action].ev_ante = ev_weight > 0.0 ? ev[action] / ev_weight : 0.0;
      row.actions[action].standard_error_ante = 0.0;
    }
    row.strategy_ev_ante = ev_weight > 0.0 ? strategy_ev / ev_weight : 0.0;
  }
  if (range_weight > 0.0) {
    for (auto &value : response.action_frequencies) {
      value /= range_weight;
    }
  }
  response.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  return Outcome::success(std::move(response));
}

std::string query_worker_response_json(const QueryWorkerResponse &response, const CompiledGame &game,
                                       const std::vector<CardId> &board) {
  nlohmann::json json;
  json["treeFingerprint"] = game.fingerprint();
  json["node"] = response.node;
  json["nodeId"] = response.node_id;
  json["street"] = street_name(response.street);
  json["player"] = response.player;
  json["visibleBoardCards"] = response.visible_board_cards;
  json["exact"] = response.exact;
  json["runouts"] = response.runouts;
  json["seconds"] = response.seconds;
  nlohmann::json cards = nlohmann::json::array();
  for (const auto card : board) {
    cards.push_back(format_card(card));
  }
  json["board"] = cards;
  json["actionIds"] = response.action_ids;
  json["actionFrequencies"] = response.action_frequencies;
  nlohmann::json rows = nlohmann::json::array();
  for (const auto &row : response.rows) {
    nlohmann::json actions = nlohmann::json::array();
    for (const auto &action : row.actions) {
      actions.push_back({{"id", action.id},
                         {"frequency", action.frequency},
                         {"evAnte", action.ev_ante},
                         {"standardErrorAnte", action.standard_error_ante}});
    }
    rows.push_back({{"hand", row.hand},
                    {"livePhysicalCombos", row.live_physical_combos},
                    {"reachable", row.reachable},
                    {"heroReach", row.hero_reach},
                    {"strategyEvAnte", row.strategy_ev_ante},
                    {"actions", actions}});
  }
  json["rows"] = rows;
  return json.dump();
}

const char *query_worker_error_name(const QueryWorkerError error) noexcept {
  switch (error) {
  case QueryWorkerError::InvalidEntry:
    return "invalid_entry";
  case QueryWorkerError::InvalidPath:
    return "invalid_path";
  case QueryWorkerError::MissingBoard:
    return "missing_board";
  case QueryWorkerError::InvalidBoard:
    return "invalid_board";
  case QueryWorkerError::TerminalReached:
    return "terminal_reached";
  case QueryWorkerError::EvaluationFailure:
    return "evaluation_failure";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
