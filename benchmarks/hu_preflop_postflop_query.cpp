#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/equity/showdown.hpp"
#include "gtosd/preflop/hu_preflop.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Json = nlohmann::json;

struct Arguments {
  std::string config;
  std::string candidate;
  std::string policy;
};

struct ReplayContext {
  gtosd::PublicState state;
  std::vector<gtosd::Action> action_prefix;
  std::array<gtosd::HuPreflopComboReach, 2> reach{};
};

std::string read_file(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open " + path);
  }
  return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

Arguments parse_arguments(const int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    if (index + 1 >= argc) {
      throw std::runtime_error("missing argument value");
    }
    const std::string_view name = argv[index++];
    const std::string value = argv[index];
    if (name == "--config") {
      arguments.config = value;
    } else if (name == "--candidate") {
      arguments.candidate = value;
    } else if (name == "--policy") {
      arguments.policy = value;
    } else {
      throw std::runtime_error("unknown argument " + std::string{name});
    }
  }
  if (arguments.config.empty() || arguments.candidate.empty() || arguments.policy.empty()) {
    throw std::runtime_error("--config, --candidate and --policy are required");
  }
  return arguments;
}

std::string preflop_action_id(const gtosd::HuPreflopTree &tree,
                              const gtosd::HuPreflopNode &node,
                              const gtosd::Action &action) {
  switch (action.type) {
  case gtosd::ActionType::Fold:
    return "fold";
  case gtosd::ActionType::Check:
    return "check";
  case gtosd::ActionType::Call:
    return "call";
  case gtosd::ActionType::AllIn:
    return "all_in";
  case gtosd::ActionType::Bet:
  case gtosd::ActionType::Raise: {
    const auto target_units =
        node.state.committed_this_street[node.state.player_to_act].units() + action.amount.units();
    const auto ante_units = tree.config.ante.units();
    if (ante_units <= 0 || target_units <= 0 || (target_units * 2) % ante_units != 0) {
      throw std::runtime_error("invalid preflop raise target");
    }
    const auto half_antes = (target_units * 2) / ante_units;
    return half_antes % 2 == 0 ? "raise_" + std::to_string(half_antes / 2)
                              : "raise_" + std::to_string(half_antes / 2) + "_5";
  }
  }
  throw std::runtime_error("unknown preflop action");
}

gtosd::HuPreflopBlueprint blueprint_from_candidate(const gtosd::HuPreflopTree &tree,
                                                    const Json &candidate) {
  if (!candidate.contains("preflop_nodes") || candidate.at("tree_fingerprint") != tree.fingerprint) {
    throw std::runtime_error("candidate does not contain a matching full preflop chart");
  }
  gtosd::HuPreflopBlueprint blueprint;
  blueprint.tree_fingerprint = tree.fingerprint;
  blueprint.algorithm = candidate.at("algorithm").get<std::string>();
  blueprint.iterations = candidate.at("iterations").get<std::uint64_t>();
  if (blueprint.iterations < 2'000'000U) {
    throw std::runtime_error("postflop viewer requires a candidate with at least 2M iterations");
  }
  const auto &charts = candidate.at("preflop_nodes");
  for (const auto &node : tree.nodes) {
    if (node.kind != gtosd::HuPreflopNodeKind::Decision) {
      continue;
    }
    const auto chart_id = node.id == tree.root ? std::string{"CO"}
                                               : "solver_node_" + std::to_string(node.id);
    const auto &chart = charts.at(chart_id);
    gtosd::HuPreflopBlueprintDecision decision;
    decision.node_id = node.id;
    decision.player = node.state.player_to_act;
    decision.action_count = static_cast<std::uint8_t>(node.edges.size());
    for (std::size_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
      const auto &row = chart.at("strategy").at(
          gtosd::class_name(static_cast<gtosd::HandClassId>(hand)));
      for (std::size_t action = 0U; action < node.edges.size(); ++action) {
        decision.strategy[hand][action] =
            row.at(preflop_action_id(tree, node, node.edges[action].action)).get<double>();
      }
    }
    blueprint.decisions.push_back(std::move(decision));
  }
  blueprint.fingerprint = gtosd::fingerprint_hu_preflop_blueprint(blueprint);
  if (!gtosd::validate_hu_preflop_blueprint(tree, blueprint)) {
    throw std::runtime_error("candidate preflop blueprint is invalid");
  }
  return blueprint;
}

gtosd::ActionConfig postflop_action_config(const gtosd::HuPreflopTree &tree) {
  gtosd::ActionConfig config;
  config.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                 tree.config.postflop_sizes.end());
  config.raise_depth = gtosd::maximum_core_raise_depth;
  config.minimum_bet = tree.config.postflop_minimum_bet;
  config.all_in_mode = gtosd::AllInMode::Add;
  config.all_in_threshold = gtosd::PotPercentage::from_basis_points(100'000).value();
  return config;
}

std::size_t visible_board_cards(const gtosd::Street street) {
  switch (street) {
  case gtosd::Street::Flop:
    return 3U;
  case gtosd::Street::Turn:
    return 4U;
  case gtosd::Street::River:
    return 5U;
  case gtosd::Street::Preflop:
    break;
  }
  throw std::runtime_error("postflop street required");
}

std::array<gtosd::CardId, 5> parse_board(const Json &request) {
  const auto &source = request.at("board");
  if (!source.is_array() || source.size() != 5U) {
    throw std::runtime_error("board must contain exactly five Short Deck cards");
  }
  std::array<gtosd::CardId, 5> board{};
  std::uint64_t mask = 0U;
  for (std::size_t index = 0U; index < board.size(); ++index) {
    const auto card = gtosd::parse_card(source[index].get<std::string>());
    if (!card || (mask & card.value().mask()) != 0U) {
      throw std::runtime_error("board contains an invalid or duplicate card");
    }
    board[index] = card.value();
    mask |= card.value().mask();
  }
  return board;
}

gtosd::Result<gtosd::PublicState, gtosd::HuPreflopError>
advance_with_board(const gtosd::PublicState &state, const std::array<gtosd::CardId, 5> &board) {
  const auto advanced = gtosd::advance_street(state);
  if (!advanced) {
    return gtosd::Result<gtosd::PublicState, gtosd::HuPreflopError>::failure(
        gtosd::HuPreflopError::GameFailure);
  }
  auto next = advanced.value();
  if (next.street == gtosd::Street::Flop) {
    next.board_mask |= board[0].mask() | board[1].mask() | board[2].mask();
  } else if (next.street == gtosd::Street::Turn) {
    next.board_mask |= board[3].mask();
  } else if (next.street == gtosd::Street::River) {
    next.board_mask |= board[4].mask();
  } else {
    return gtosd::Result<gtosd::PublicState, gtosd::HuPreflopError>::failure(
        gtosd::HuPreflopError::IntegrityFailure);
  }
  return gtosd::validate_state(next)
             ? gtosd::Result<gtosd::PublicState, gtosd::HuPreflopError>::success(next)
             : gtosd::Result<gtosd::PublicState, gtosd::HuPreflopError>::failure(
                   gtosd::HuPreflopError::GameFailure);
}

std::array<gtosd::CardId, 5>
placeholder_board(const std::array<gtosd::CardId, 5> &board, const std::size_t visible,
                  const std::uint64_t extra_dead_mask) {
  auto result = board;
  std::uint64_t mask = extra_dead_mask;
  for (std::size_t index = 0U; index < visible; ++index) {
    mask |= result[index].mask();
  }
  const auto deck = gtosd::short_deck();
  std::size_t cursor = 0U;
  for (std::size_t index = visible; index < result.size(); ++index) {
    while (cursor < deck.size() && (mask & deck[cursor].mask()) != 0U) {
      ++cursor;
    }
    if (cursor == deck.size()) {
      throw std::runtime_error("cannot construct a valid placeholder board");
    }
    result[index] = deck[cursor++];
    mask |= result[index].mask();
  }
  return result;
}

std::string street_name(const gtosd::Street street) {
  switch (street) {
  case gtosd::Street::Flop:
    return "flop";
  case gtosd::Street::Turn:
    return "turn";
  case gtosd::Street::River:
    return "river";
  case gtosd::Street::Preflop:
    return "preflop";
  }
  return "unknown";
}

class PostflopQueryEngine {
public:
  PostflopQueryEngine(gtosd::HuPreflopTree tree, gtosd::HuPreflopBlueprint blueprint,
                      gtosd::HuPreflopDecompositionPlan decomposition,
                      gtosd::HuPreflopSampledPostflopPolicy policy)
      : tree_(std::move(tree)), blueprint_(std::move(blueprint)),
        decomposition_(std::move(decomposition)), policy_(std::move(policy)),
        action_config_(postflop_action_config(tree_)), combos_(gtosd::all_combos()) {
    for (std::size_t combo = 0U; combo < combos_.size(); ++combo) {
      combo_masks_[combo] = combos_[combo].first.mask() | combos_[combo].second.mask();
      all_combo_ids_[combo] = static_cast<gtosd::ComboId>(combo);
      combos_by_class_[gtosd::hand_class(combos_[combo])].push_back(
          static_cast<gtosd::ComboId>(combo));
    }
  }

  Json ready() const {
    return {{"status", "ready"},
            {"schema", "gtosd.hu_postflop_query_worker.v1"},
            {"treeFingerprint", tree_.fingerprint},
            {"policyFingerprint", policy_.fingerprint},
            {"iterations", policy_.iterations},
            {"abstraction", policy_.abstraction_id}};
  }

  Json query(const Json &request) const;

private:
  ReplayContext replay(std::uint32_t entry_node, const std::vector<std::size_t> &action_indices,
                       const std::array<gtosd::CardId, 5> &board) const;
  double action_probability(std::uint32_t entry_node,
                            const std::array<gtosd::CardId, 5> &board,
                            const gtosd::PublicState &state,
                            std::span<const gtosd::Action> prefix,
                            const gtosd::Action &action, gtosd::ComboId combo) const;
  double terminal_payoff(const gtosd::PublicState &state,
                         const std::array<gtosd::ComboId, 2> &hole_combos,
                         const std::array<gtosd::CardId, 5> &board,
                         std::uint8_t player) const;
  double rollout(std::uint32_t entry_node, gtosd::PublicState state,
                 std::vector<gtosd::Action> prefix,
                 const std::array<gtosd::ComboId, 2> &hole_combos,
                 const std::array<gtosd::CardId, 5> &board, std::uint8_t player,
                 std::mt19937_64 &random) const;
  std::array<gtosd::CardId, 5>
  sample_runout(const std::array<gtosd::CardId, 5> &requested_board, std::size_t visible,
                std::uint64_t hole_mask, std::mt19937_64 &random) const;
  gtosd::ComboId sample_combo(std::span<const gtosd::ComboId> candidates,
                              const gtosd::HuPreflopComboReach &reach, std::uint64_t dead_mask,
                              bool uniform_fallback, std::mt19937_64 &random) const;

  gtosd::HuPreflopTree tree_;
  gtosd::HuPreflopBlueprint blueprint_;
  gtosd::HuPreflopDecompositionPlan decomposition_;
  gtosd::HuPreflopSampledPostflopPolicy policy_;
  gtosd::ActionConfig action_config_;
  std::array<gtosd::Combo, 630U> combos_{};
  std::array<std::uint64_t, 630U> combo_masks_{};
  std::array<gtosd::ComboId, 630U> all_combo_ids_{};
  std::array<std::vector<gtosd::ComboId>, gtosd::hu_preflop_hand_class_count> combos_by_class_{};
};

double PostflopQueryEngine::action_probability(
    const std::uint32_t entry_node, const std::array<gtosd::CardId, 5> &board,
    const gtosd::PublicState &state, const std::span<const gtosd::Action> prefix,
    const gtosd::Action &action, const gtosd::ComboId combo) const {
  const auto probability = gtosd::query_hu_preflop_sampled_postflop_action_probability(
      tree_, policy_, entry_node, board, state, prefix, action, combo);
  if (!probability || !std::isfinite(probability.value()) || probability.value() < 0.0 ||
      probability.value() > 1.0) {
    throw std::runtime_error("sampled policy query failed");
  }
  return probability.value();
}

ReplayContext PostflopQueryEngine::replay(
    const std::uint32_t entry_node, const std::vector<std::size_t> &action_indices,
    const std::array<gtosd::CardId, 5> &board) const {
  if (entry_node >= tree_.nodes.size() ||
      tree_.nodes[entry_node].kind != gtosd::HuPreflopNodeKind::PostflopEntry) {
    throw std::runtime_error("invalid postflop entry");
  }
  const auto entry = std::ranges::find_if(decomposition_.entries, [entry_node](const auto &item) {
    return item.entry_node == entry_node;
  });
  if (entry == decomposition_.entries.end()) {
    throw std::runtime_error("postflop entry is absent from decomposition");
  }
  ReplayContext result;
  result.state = tree_.nodes[entry_node].state;
  result.reach = entry->own_sequence_reach;
  const auto flop = advance_with_board(result.state, board);
  if (!flop) {
    throw std::runtime_error("cannot advance entry to flop");
  }
  result.state = flop.value();
  for (const auto action_index : action_indices) {
    while (result.state.status == gtosd::HandStatus::StreetComplete) {
      const auto next = advance_with_board(result.state, board);
      if (!next) {
        throw std::runtime_error("cannot advance replay street");
      }
      result.state = next.value();
    }
    if (result.state.status != gtosd::HandStatus::InProgress) {
      throw std::runtime_error("action path continues after a terminal state");
    }
    const auto actions = gtosd::legal_actions(result.state, action_config_);
    if (!actions || action_index >= actions.value().size()) {
      throw std::runtime_error("action path contains an invalid action index");
    }
    const auto action = actions.value()[action_index];
    const auto visible = visible_board_cards(result.state.street);
    for (std::size_t combo = 0U; combo < combos_.size(); ++combo) {
      const auto player = result.state.player_to_act;
      if (result.reach[player][combo] <= 0.0 ||
          (combo_masks_[combo] & result.state.board_mask) != 0U) {
        continue;
      }
      const auto concrete_board = placeholder_board(board, visible, combo_masks_[combo]);
      result.reach[player][combo] *= action_probability(
          entry_node, concrete_board, result.state, result.action_prefix, action,
          static_cast<gtosd::ComboId>(combo));
    }
    const auto next = gtosd::apply_action(result.state, action, action_config_);
    if (!next) {
      throw std::runtime_error("cannot apply replay action");
    }
    result.action_prefix.push_back(action);
    result.state = next.value();
  }
  while (result.state.status == gtosd::HandStatus::StreetComplete) {
    const auto next = advance_with_board(result.state, board);
    if (!next) {
      throw std::runtime_error("cannot advance current street");
    }
    result.state = next.value();
  }
  if (result.state.status != gtosd::HandStatus::InProgress) {
    throw std::runtime_error("selected path is terminal and has no strategy grid");
  }
  return result;
}

gtosd::ComboId PostflopQueryEngine::sample_combo(
    const std::span<const gtosd::ComboId> candidates, const gtosd::HuPreflopComboReach &reach,
    const std::uint64_t dead_mask, const bool uniform_fallback, std::mt19937_64 &random) const {
  std::vector<double> weights;
  weights.reserve(candidates.size());
  double total = 0.0;
  for (const auto combo : candidates) {
    const auto weight = (combo_masks_[combo] & dead_mask) == 0U
                            ? (uniform_fallback ? 1.0 : reach[combo])
                            : 0.0;
    weights.push_back(weight);
    total += weight;
  }
  if (!(total > 0.0)) {
    throw std::runtime_error("conditioned range has no compatible physical combo");
  }
  std::discrete_distribution<std::size_t> distribution(weights.begin(), weights.end());
  return candidates[distribution(random)];
}

std::array<gtosd::CardId, 5> PostflopQueryEngine::sample_runout(
    const std::array<gtosd::CardId, 5> &requested_board, const std::size_t visible,
    const std::uint64_t hole_mask, std::mt19937_64 &random) const {
  auto board = requested_board;
  std::uint64_t dead_mask = hole_mask;
  for (std::size_t index = 0U; index < visible; ++index) {
    dead_mask |= board[index].mask();
  }
  std::vector<gtosd::CardId> available;
  for (const auto card : gtosd::short_deck()) {
    if ((dead_mask & card.mask()) == 0U) {
      available.push_back(card);
    }
  }
  std::shuffle(available.begin(), available.end(), random);
  for (std::size_t index = visible; index < board.size(); ++index) {
    board[index] = available[index - visible];
  }
  return board;
}

double PostflopQueryEngine::terminal_payoff(
    const gtosd::PublicState &state, const std::array<gtosd::ComboId, 2> &hole_combos,
    const std::array<gtosd::CardId, 5> &board, const std::uint8_t player) const {
  std::uint8_t winner_mask = 0U;
  if (state.status != gtosd::HandStatus::Folded) {
    const std::vector<std::array<gtosd::CardId, 2>> holes{
        {combos_[hole_combos[0]].first, combos_[hole_combos[0]].second},
        {combos_[hole_combos[1]].first, combos_[hole_combos[1]].second}};
    const std::vector<gtosd::CardId> public_board(board.begin(), board.end());
    const auto showdown = gtosd::evaluate_showdown(holes, public_board);
    if (!showdown) {
      throw std::runtime_error("showdown evaluation failed");
    }
    winner_mask = showdown.value().winner_mask;
  }
  const auto settlement = gtosd::settle_terminal(state, tree_.config.rake, winner_mask);
  if (!settlement) {
    throw std::runtime_error("terminal settlement failed");
  }
  return static_cast<double>(settlement.value().payoff_units[player]) /
         static_cast<double>(gtosd::Money::units_per_ante);
}

double PostflopQueryEngine::rollout(
    const std::uint32_t entry_node, gtosd::PublicState state,
    std::vector<gtosd::Action> prefix, const std::array<gtosd::ComboId, 2> &hole_combos,
    const std::array<gtosd::CardId, 5> &board, const std::uint8_t player,
    std::mt19937_64 &random) const {
  while (true) {
    if (state.status == gtosd::HandStatus::Folded ||
        state.status == gtosd::HandStatus::AllInRunout ||
        state.status == gtosd::HandStatus::Showdown) {
      return terminal_payoff(state, hole_combos, board, player);
    }
    if (state.status == gtosd::HandStatus::StreetComplete) {
      const auto next = advance_with_board(state, board);
      if (!next) {
        throw std::runtime_error("rollout cannot advance street");
      }
      state = next.value();
      continue;
    }
    const auto actions = gtosd::legal_actions(state, action_config_);
    if (!actions || actions.value().empty()) {
      throw std::runtime_error("rollout has no legal action");
    }
    std::vector<double> probabilities;
    probabilities.reserve(actions.value().size());
    for (const auto &action : actions.value()) {
      probabilities.push_back(action_probability(entry_node, board, state, prefix, action,
                                                  hole_combos[state.player_to_act]));
    }
    std::discrete_distribution<std::size_t> distribution(probabilities.begin(),
                                                         probabilities.end());
    const auto selected = actions.value()[distribution(random)];
    const auto next = gtosd::apply_action(state, selected, action_config_);
    if (!next) {
      throw std::runtime_error("rollout cannot apply policy action");
    }
    prefix.push_back(selected);
    state = next.value();
  }
}

Json PostflopQueryEngine::query(const Json &request) const {
  const auto entry_node = request.at("entryNode").get<std::uint32_t>();
  const auto action_indices = request.value("actionIndices", std::vector<std::size_t>{});
  const auto requested_board = parse_board(request);
  const auto replayed = replay(entry_node, action_indices, requested_board);
  const auto actions = gtosd::legal_actions(replayed.state, action_config_);
  if (!actions || actions.value().empty()) {
    throw std::runtime_error("current node has no legal actions");
  }
  const auto samples_per_action =
      std::clamp(request.value("samplesPerAction", 256U), 32U, 4'096U);
  const auto base_seed = request.value("seed", 0x5649'4557'4552'0001ULL);
  const auto visible = visible_board_cards(replayed.state.street);
  std::uint64_t visible_mask = 0U;
  for (std::size_t index = 0U; index < visible; ++index) {
    visible_mask |= requested_board[index].mask();
  }
  const auto actor = replayed.state.player_to_act;
  const auto opponent = static_cast<std::uint8_t>(1U - actor);

  std::array<Json, gtosd::hu_preflop_hand_class_count> row_results{};
  std::array<std::vector<double>, gtosd::hu_preflop_hand_class_count> row_frequencies{};
  std::array<double, gtosd::hu_preflop_hand_class_count> row_reaches{};
  std::atomic<std::size_t> next_hand{0U};
  std::atomic<bool> failed{false};
  std::exception_ptr worker_error;
  std::mutex error_mutex;
  const auto solve_hand = [&](const std::size_t hand) {
    const auto &class_combos = combos_by_class_[hand];
    double class_reach = 0.0;
    std::size_t live_combos = 0U;
    for (const auto combo : class_combos) {
      if ((combo_masks_[combo] & visible_mask) == 0U) {
        class_reach += replayed.reach[actor][combo];
        ++live_combos;
      }
    }
    const auto uniform_fallback = !(class_reach > 0.0);
    const auto denominator = uniform_fallback ? static_cast<double>(live_combos) : class_reach;
    std::vector<double> frequencies(actions.value().size(), 0.0);
    if (denominator > 0.0) {
      for (const auto combo : class_combos) {
        if ((combo_masks_[combo] & visible_mask) != 0U) {
          continue;
        }
        const auto weight = uniform_fallback ? 1.0 : replayed.reach[actor][combo];
        if (!(weight > 0.0)) {
          continue;
        }
        const auto concrete_board = placeholder_board(requested_board, visible, combo_masks_[combo]);
        for (std::size_t action = 0U; action < actions.value().size(); ++action) {
          frequencies[action] += weight * action_probability(
                                             entry_node, concrete_board, replayed.state,
                                             replayed.action_prefix, actions.value()[action], combo);
        }
      }
      for (auto &frequency : frequencies) {
        frequency /= denominator;
      }
    }

    Json action_rows = Json::array();
    double strategy_ev = 0.0;
    for (std::size_t action = 0U; action < actions.value().size(); ++action) {
      std::mt19937_64 random(base_seed ^ (static_cast<std::uint64_t>(hand + 1U) << 32U) ^
                             static_cast<std::uint64_t>(action + 1U));
      double sum = 0.0;
      double sum_squares = 0.0;
      for (std::uint32_t sample = 0U; sample < samples_per_action; ++sample) {
        const auto target = sample_combo(class_combos, replayed.reach[actor], visible_mask,
                                         uniform_fallback, random);
        const auto villain = sample_combo(all_combo_ids_, replayed.reach[opponent],
                                          visible_mask | combo_masks_[target], false, random);
        std::array<gtosd::ComboId, 2> hole_combos{};
        hole_combos[actor] = target;
        hole_combos[opponent] = villain;
        const auto board = sample_runout(requested_board, visible,
                                         combo_masks_[target] | combo_masks_[villain], random);
        const auto next = gtosd::apply_action(replayed.state, actions.value()[action], action_config_);
        if (!next) {
          throw std::runtime_error("cannot force current action");
        }
        auto prefix = replayed.action_prefix;
        prefix.push_back(actions.value()[action]);
        const auto value = rollout(entry_node, next.value(), std::move(prefix), hole_combos, board,
                                   actor, random);
        sum += value;
        sum_squares += value * value;
      }
      const auto samples = static_cast<double>(samples_per_action);
      const auto mean = sum / samples;
      const auto variance = samples_per_action > 1U
                                ? std::max(0.0, (sum_squares - sum * sum / samples) /
                                                    static_cast<double>(samples_per_action - 1U))
                                : 0.0;
      const auto standard_error = std::sqrt(variance / samples);
      strategy_ev += frequencies[action] * mean;
      action_rows.push_back({{"frequency", frequencies[action]},
                             {"evAnte", mean},
                             {"standardErrorAnte", standard_error},
                             {"samples", samples_per_action}});
    }
    row_reaches[hand] = class_reach;
    row_frequencies[hand] = frequencies;
    row_results[hand] = {
        {"hand", gtosd::class_name(static_cast<gtosd::HandClassId>(hand))},
        {"livePhysicalCombos", live_combos},
        {"reachWeight", class_reach},
        {"reachable", !uniform_fallback},
        {"strategyEvAnte", strategy_ev},
        {"actions", std::move(action_rows)}};
  };
  const auto requested_workers = std::clamp(request.value("workerThreads", 8U), 1U, 8U);
  {
    std::vector<std::jthread> workers;
    workers.reserve(requested_workers);
    for (std::uint32_t worker = 0U; worker < requested_workers; ++worker) {
      workers.emplace_back([&] {
        while (!failed.load(std::memory_order_relaxed)) {
          const auto hand = next_hand.fetch_add(1U, std::memory_order_relaxed);
          if (hand >= gtosd::hu_preflop_hand_class_count) {
            return;
          }
          try {
            solve_hand(hand);
          } catch (...) {
            {
              const std::scoped_lock lock(error_mutex);
              if (!worker_error) {
                worker_error = std::current_exception();
              }
            }
            failed.store(true, std::memory_order_relaxed);
            return;
          }
        }
      });
    }
  }
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }

  Json rows = Json::array();
  std::vector<double> aggregate_action_mass(actions.value().size(), 0.0);
  double aggregate_reach = 0.0;
  for (std::size_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
    rows.push_back(std::move(row_results[hand]));
    aggregate_reach += row_reaches[hand];
    if (!(row_reaches[hand] > 0.0)) {
      continue;
    }
    for (std::size_t action = 0U; action < actions.value().size(); ++action) {
      aggregate_action_mass[action] += row_reaches[hand] * row_frequencies[hand][action];
    }
  }

  Json action_frequencies = Json::array();
  for (const auto mass : aggregate_action_mass) {
    action_frequencies.push_back(aggregate_reach > 0.0 ? mass / aggregate_reach : 0.0);
  }
  return {{"schema", "gtosd.hu_postflop_combo_grid.v1"},
          {"treeFingerprint", tree_.fingerprint},
          {"policyFingerprint", policy_.fingerprint},
          {"iterations", policy_.iterations},
          {"abstraction", policy_.abstraction_id},
          {"entryNode", entry_node},
          {"actionIndices", action_indices},
          {"street", street_name(replayed.state.street)},
          {"player", actor == 0U ? "CO" : "BTN"},
          {"board",
           Json::array({gtosd::format_card(requested_board[0]),
                        gtosd::format_card(requested_board[1]),
                        gtosd::format_card(requested_board[2]),
                        gtosd::format_card(requested_board[3]),
                        gtosd::format_card(requested_board[4])})},
          {"visibleBoardCards", visible},
          {"samplesPerAction", samples_per_action},
          {"workerThreads", requested_workers},
          {"evScope",
           "conditional_on_public_history_and_visible_board_average_policy_monte_carlo_runout_v1"},
          {"actionFrequencies", std::move(action_frequencies)},
          {"rows", std::move(rows)}};
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto arguments = parse_arguments(argc, argv);
    const auto config = gtosd::deserialize_hu_preflop_config_json(read_file(arguments.config));
    if (!config) {
      throw std::runtime_error("invalid HU preflop config");
    }
    auto tree = gtosd::build_hu_preflop_tree(config.value());
    if (!tree) {
      throw std::runtime_error("cannot build HU preflop tree");
    }
    const auto candidate = Json::parse(read_file(arguments.candidate));
    auto blueprint = blueprint_from_candidate(tree.value(), candidate);
    auto decomposition = gtosd::derive_hu_preflop_decomposition_plan(tree.value(), blueprint);
    if (!decomposition) {
      throw std::runtime_error("cannot derive preflop reached ranges");
    }
    auto policy = gtosd::load_hu_preflop_sampled_postflop_policy(tree.value(), arguments.policy);
    if (!policy || policy.value().iterations < 2'000'000U ||
        policy.value().iterations != blueprint.iterations) {
      throw std::runtime_error("invalid or iteration-mismatched 2M postflop policy");
    }
    PostflopQueryEngine engine(std::move(tree.value()), std::move(blueprint),
                               std::move(decomposition.value()), std::move(policy.value()));
    std::cout << engine.ready().dump() << '\n' << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.empty()) {
        continue;
      }
      try {
        const auto request = Json::parse(line);
        std::cout << engine.query(request).dump() << '\n' << std::flush;
      } catch (const std::exception &error) {
        std::cout << Json{{"error", error.what()}}.dump() << '\n' << std::flush;
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HU_POSTFLOP_QUERY=FAIL error=" << error.what() << '\n';
    return 1;
  }
}
