#include "gtosd/solver/reference_games.hpp"

#include "gtosd/equity/showdown.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gtosd {
namespace {

constexpr GameActionId action_check = 0;
constexpr GameActionId action_bet_or_raise = 1;
constexpr GameActionId action_fold = 2;
constexpr GameActionId action_call = 3;

class GameBuilder {
public:
  GameNodeId terminal(const std::array<double, 2> payoff) {
    GameNode node;
    node.kind = GameNodeKind::Terminal;
    node.payoff = payoff;
    return append(std::move(node));
  }

  GameNodeId chance(std::vector<GameEdge> edges) {
    GameNode node;
    node.kind = GameNodeKind::Chance;
    node.edges = std::move(edges);
    return append(std::move(node));
  }

  GameNodeId decision(const std::uint8_t player, std::string information_set,
                      std::vector<GameEdge> edges) {
    GameNode node;
    node.kind = GameNodeKind::Decision;
    node.player = player;
    node.information_set = std::move(information_set);
    node.edges = std::move(edges);
    return append(std::move(node));
  }

  std::vector<GameNode> finish() { return std::move(nodes_); }

private:
  GameNodeId append(GameNode node) {
    const auto id = static_cast<GameNodeId>(nodes_.size());
    nodes_.push_back(std::move(node));
    return id;
  }

  std::vector<GameNode> nodes_;
};

GameEdge edge(const GameActionId action, const char *const label, const GameNodeId child,
              const double probability = 0.0) {
  return {{action, label}, child, probability};
}

char kuhn_rank_name(const std::uint8_t rank) {
  constexpr std::array<char, 3> names{'J', 'Q', 'K'};
  return names[rank];
}

std::array<double, 2> kuhn_showdown(const std::uint8_t first_rank, const std::uint8_t second_rank,
                                    const double stake) {
  return first_rank > second_rank ? std::array<double, 2>{stake, -stake}
                                  : std::array<double, 2>{-stake, stake};
}

GameNodeId build_kuhn_deal(GameBuilder &builder, const std::uint8_t first_rank,
                           const std::uint8_t second_rank) {
  const std::string first(1, kuhn_rank_name(first_rank));
  const std::string second(1, kuhn_rank_name(second_rank));
  const auto check_check = builder.terminal(kuhn_showdown(first_rank, second_rank, 1.0));
  const auto bet_fold = builder.terminal({1.0, -1.0});
  const auto bet_call = builder.terminal(kuhn_showdown(first_rank, second_rank, 2.0));
  const auto check_bet_fold = builder.terminal({-1.0, 1.0});
  const auto check_bet_call = builder.terminal(kuhn_showdown(first_rank, second_rank, 2.0));

  const auto first_facing_bet = builder.decision(
      0, "kuhn:p0:" + first + ":kb",
      {edge(action_fold, "fold", check_bet_fold), edge(action_call, "call", check_bet_call)});
  const auto second_after_check =
      builder.decision(1, "kuhn:p1:" + second + ":k",
                       {edge(action_check, "check", check_check),
                        edge(action_bet_or_raise, "bet", first_facing_bet)});
  const auto second_facing_bet =
      builder.decision(1, "kuhn:p1:" + second + ":b",
                       {edge(action_fold, "fold", bet_fold), edge(action_call, "call", bet_call)});
  return builder.decision(0, "kuhn:p0:" + first + ":",
                          {edge(action_check, "check", second_after_check),
                           edge(action_bet_or_raise, "bet", second_facing_bet)});
}

struct LeducState {
  std::array<std::uint8_t, 2> private_cards{};
  std::uint8_t public_card{6};
  std::uint8_t round{0};
  std::uint8_t player_to_act{0};
  std::array<int, 2> round_contribution{0, 0};
  std::array<int, 2> total_contribution{1, 1};
  int current_bet{0};
  std::uint8_t aggressive_actions{0};
  bool first_check{false};
  std::string history;
};

std::uint8_t leduc_rank(const std::uint8_t physical_card) {
  return static_cast<std::uint8_t>(physical_card / 2U);
}

std::string leduc_information_set(const LeducState &state) {
  const auto player = state.player_to_act;
  const char private_rank = kuhn_rank_name(leduc_rank(state.private_cards[player]));
  const char public_rank =
      state.public_card < 6U ? kuhn_rank_name(leduc_rank(state.public_card)) : '-';
  return "leduc:p" + std::to_string(player) + ":" + private_rank + ":" + public_rank + ":r" +
         std::to_string(state.round) + ":" + state.history;
}

std::array<double, 2> leduc_terminal_payoff(const LeducState &state, const std::uint8_t winner) {
  const double pot = static_cast<double>(state.total_contribution[0] + state.total_contribution[1]);
  std::array<double, 2> payoff{-static_cast<double>(state.total_contribution[0]),
                               -static_cast<double>(state.total_contribution[1])};
  payoff[winner] += pot;
  return payoff;
}

std::array<double, 2> leduc_showdown_payoff(const LeducState &state) {
  const auto first_rank = leduc_rank(state.private_cards[0]);
  const auto second_rank = leduc_rank(state.private_cards[1]);
  const auto public_rank = leduc_rank(state.public_card);
  const int first_score = (first_rank == public_rank ? 100 : 0) + first_rank;
  const int second_score = (second_rank == public_rank ? 100 : 0) + second_rank;
  if (first_score > second_score) {
    return leduc_terminal_payoff(state, 0);
  }
  if (second_score > first_score) {
    return leduc_terminal_payoff(state, 1);
  }
  const double pot = static_cast<double>(state.total_contribution[0] + state.total_contribution[1]);
  return {pot / 2.0 - static_cast<double>(state.total_contribution[0]),
          pot / 2.0 - static_cast<double>(state.total_contribution[1])};
}

GameNodeId build_leduc_betting(GameBuilder &builder, const LeducState &state);

GameNodeId finish_leduc_round(GameBuilder &builder, const LeducState &state) {
  if (state.round == 1U) {
    return builder.terminal(leduc_showdown_payoff(state));
  }
  std::vector<GameEdge> public_cards;
  for (std::uint8_t card = 0; card < 6U; ++card) {
    if (card == state.private_cards[0] || card == state.private_cards[1]) {
      continue;
    }
    LeducState next = state;
    next.public_card = card;
    next.round = 1;
    next.player_to_act = 0;
    next.round_contribution = {0, 0};
    next.current_bet = 0;
    next.aggressive_actions = 0;
    next.first_check = false;
    next.history += "/";
    public_cards.push_back(edge(card, ("public_" + std::to_string(card)).c_str(),
                                build_leduc_betting(builder, next), 0.25));
  }
  return builder.chance(std::move(public_cards));
}

GameNodeId build_leduc_betting(GameBuilder &builder, const LeducState &state) {
  const std::uint8_t actor = state.player_to_act;
  const std::uint8_t opponent = static_cast<std::uint8_t>(1U - actor);
  const int bet_size = state.round == 0U ? 2 : 4;
  std::vector<GameEdge> actions;

  if (state.current_bet == state.round_contribution[actor]) {
    LeducState checked = state;
    checked.history += "k";
    if (state.first_check) {
      actions.push_back(edge(action_check, "check", finish_leduc_round(builder, checked)));
    } else {
      checked.first_check = true;
      checked.player_to_act = opponent;
      actions.push_back(edge(action_check, "check", build_leduc_betting(builder, checked)));
    }

    LeducState bet = state;
    bet.current_bet += bet_size;
    bet.round_contribution[actor] += bet_size;
    bet.total_contribution[actor] += bet_size;
    bet.aggressive_actions = 1;
    bet.first_check = false;
    bet.player_to_act = opponent;
    bet.history += "b";
    actions.push_back(edge(action_bet_or_raise, "bet", build_leduc_betting(builder, bet)));
  } else {
    LeducState folded = state;
    folded.history += "f";
    actions.push_back(
        edge(action_fold, "fold", builder.terminal(leduc_terminal_payoff(folded, opponent))));

    LeducState called = state;
    const int call_amount = state.current_bet - state.round_contribution[actor];
    called.round_contribution[actor] += call_amount;
    called.total_contribution[actor] += call_amount;
    called.history += "c";
    actions.push_back(edge(action_call, "call", finish_leduc_round(builder, called)));

    if (state.aggressive_actions < 2U) {
      LeducState raised = state;
      raised.current_bet += bet_size;
      const int target = raised.current_bet;
      const int raise_amount = target - raised.round_contribution[actor];
      raised.round_contribution[actor] = target;
      raised.total_contribution[actor] += raise_amount;
      ++raised.aggressive_actions;
      raised.player_to_act = opponent;
      raised.history += "r";
      actions.push_back(edge(action_bet_or_raise, "raise", build_leduc_betting(builder, raised)));
    }
  }

  return builder.decision(actor, leduc_information_set(state), std::move(actions));
}

std::array<double, 2> short_deck_toy_payoff(const std::uint8_t winner,
                                            const std::array<double, 2> contributions,
                                            const double rake_fraction) {
  const double called_pot = contributions[0] + contributions[1];
  const double rake = called_pot * rake_fraction;
  std::array<double, 2> payoff{-contributions[0], -contributions[1]};
  payoff[winner] += called_pot - rake;
  return payoff;
}

GameNodeId build_short_deck_toy_deal(GameBuilder &builder, const std::uint8_t showdown_winner,
                                     const double rake_fraction) {
  const char first_type = showdown_winner == 0U ? 'S' : 'W';
  const char second_type = showdown_winner == 1U ? 'S' : 'W';

  const auto check_check =
      builder.terminal(short_deck_toy_payoff(showdown_winner, {1.0, 1.0}, rake_fraction));
  const auto first_bet_fold = builder.terminal(short_deck_toy_payoff(0, {1.0, 1.0}, rake_fraction));
  const auto first_bet_call =
      builder.terminal(short_deck_toy_payoff(showdown_winner, {2.0, 2.0}, rake_fraction));
  const auto second_bet_fold =
      builder.terminal(short_deck_toy_payoff(1, {1.0, 1.0}, rake_fraction));
  const auto second_bet_call =
      builder.terminal(short_deck_toy_payoff(showdown_winner, {2.0, 2.0}, rake_fraction));

  const auto first_facing = builder.decision(
      0, "short_deck_toy:p0:" + std::string(1, first_type) + ":kb",
      {edge(action_fold, "fold", second_bet_fold), edge(action_call, "call", second_bet_call)});
  const auto second_after_check = builder.decision(
      1, "short_deck_toy:p1:" + std::string(1, second_type) + ":k",
      {edge(action_check, "check", check_check), edge(action_bet_or_raise, "bet", first_facing)});
  const auto second_facing = builder.decision(
      1, "short_deck_toy:p1:" + std::string(1, second_type) + ":b",
      {edge(action_fold, "fold", first_bet_fold), edge(action_call, "call", first_bet_call)});
  return builder.decision(0, "short_deck_toy:p0:" + std::string(1, first_type) + ":",
                          {edge(action_check, "check", second_after_check),
                           edge(action_bet_or_raise, "bet", second_facing)});
}

struct ActionProbability {
  GameActionId action{0};
  double probability{0.0};
};

bool set_action_probability(StrategyProfile &profile, const std::string &information_set,
                            const ActionProbability assignment) {
  const auto found = profile.find(information_set);
  if (found == profile.end()) {
    return false;
  }
  for (std::size_t index = 0; index < found->second.actions.size(); ++index) {
    if (found->second.actions[index] == assignment.action) {
      found->second.probabilities[index] = assignment.probability;
      return true;
    }
  }
  return false;
}

void set_binary_strategy(StrategyProfile &profile, const std::string &information_set,
                         const ActionProbability first, const ActionProbability second) {
  if (!set_action_probability(profile, information_set, first) ||
      !set_action_probability(profile, information_set, second)) {
    throw std::logic_error("reference strategy information set mismatch");
  }
}

} // namespace

FiniteGame make_matching_pennies_game() {
  GameBuilder builder;
  const auto heads_heads = builder.terminal({1.0, -1.0});
  const auto heads_tails = builder.terminal({-1.0, 1.0});
  const auto tails_heads = builder.terminal({-1.0, 1.0});
  const auto tails_tails = builder.terminal({1.0, -1.0});
  const auto after_heads = builder.decision(
      1, "matching:p1", {edge(0, "heads", heads_heads), edge(1, "tails", heads_tails)});
  const auto after_tails = builder.decision(
      1, "matching:p1", {edge(0, "heads", tails_heads), edge(1, "tails", tails_tails)});
  const auto root = builder.decision(
      0, "matching:p0", {edge(0, "heads", after_heads), edge(1, "tails", after_tails)});
  return {"matching_pennies_v1", root, builder.finish(), 1.0};
}

FiniteGame make_kuhn_poker_game() {
  GameBuilder builder;
  std::vector<GameEdge> deals;
  GameActionId deal_action = 100;
  for (std::uint8_t first = 0; first < 3U; ++first) {
    for (std::uint8_t second = 0; second < 3U; ++second) {
      if (first == second) {
        continue;
      }
      const std::string label =
          "deal_" + std::string(1, kuhn_rank_name(first)) + kuhn_rank_name(second);
      deals.push_back(
          edge(deal_action++, label.c_str(), build_kuhn_deal(builder, first, second), 1.0 / 6.0));
    }
  }
  const auto root = builder.chance(std::move(deals));
  return {"kuhn_poker_v1", root, builder.finish(), 2.0};
}

FiniteGame make_leduc_poker_game() {
  GameBuilder builder;
  std::vector<GameEdge> deals;
  GameActionId deal_action = 200;
  for (std::uint8_t first = 0; first < 6U; ++first) {
    for (std::uint8_t second = 0; second < 6U; ++second) {
      if (first == second) {
        continue;
      }
      LeducState state;
      state.private_cards = {first, second};
      const std::string label = "deal_" + std::to_string(first) + "_" + std::to_string(second);
      deals.push_back(
          edge(deal_action++, label.c_str(), build_leduc_betting(builder, state), 1.0 / 30.0));
    }
  }
  const auto root = builder.chance(std::move(deals));
  return {"leduc_poker_v1", root, builder.finish(), 2.0};
}

Result<FiniteGame, SolverError> make_short_deck_river_toy_game(const double rake_fraction) {
  if (!std::isfinite(rake_fraction) || rake_fraction < 0.0 || rake_fraction > 0.25) {
    return Result<FiniteGame, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  constexpr std::array<std::string_view, 5> board_text{"As", "Qd", "9c", "8h", "6s"};
  constexpr std::array<std::array<std::string_view, 2>, 2> hand_text{{
      {"7d", "Kc"},
      {"Ah", "Qc"},
  }};
  std::vector<CardId> board;
  std::vector<std::array<CardId, 2>> first_deal;
  board.reserve(board_text.size());
  first_deal.reserve(hand_text.size());
  for (const auto text : board_text) {
    const auto parsed = parse_card(text);
    if (!parsed) {
      return Result<FiniteGame, SolverError>::failure(SolverError::InvalidGame);
    }
    board.push_back(parsed.value());
  }
  for (const auto &hand : hand_text) {
    const auto first = parse_card(hand[0]);
    const auto second = parse_card(hand[1]);
    if (!first || !second) {
      return Result<FiniteGame, SolverError>::failure(SolverError::InvalidGame);
    }
    first_deal.push_back({first.value(), second.value()});
  }
  const auto first_showdown = evaluate_showdown(first_deal, board);
  std::ranges::reverse(first_deal);
  const auto second_showdown = evaluate_showdown(first_deal, board);
  if (!first_showdown || !second_showdown || first_showdown.value().winner_mask == 0U ||
      second_showdown.value().winner_mask == 0U) {
    return Result<FiniteGame, SolverError>::failure(SolverError::InvalidGame);
  }
  const std::uint8_t first_winner = (first_showdown.value().winner_mask & 0b01U) != 0U ? 0U : 1U;
  const std::uint8_t second_winner = (second_showdown.value().winner_mask & 0b01U) != 0U ? 0U : 1U;
  GameBuilder builder;
  const auto strong_weak = build_short_deck_toy_deal(builder, first_winner, rake_fraction);
  const auto weak_strong = build_short_deck_toy_deal(builder, second_winner, rake_fraction);
  const auto root = builder.chance({edge(300, "deal_strong_weak", strong_weak, 0.5),
                                    edge(301, "deal_weak_strong", weak_strong, 0.5)});
  FiniteGame game{"short_deck_river_toy_v1_rake_" + std::to_string(rake_fraction), root,
                  builder.finish(), 2.0};
  return Result<FiniteGame, SolverError>::success(std::move(game));
}

Result<StrategyProfile, SolverError> reference_equilibrium_strategy(const FiniteGame &game) {
  auto uniform = uniform_strategy_profile(game);
  if (!uniform) {
    return uniform;
  }
  StrategyProfile profile = uniform.value();
  if (game.game_id == "matching_pennies_v1") {
    return Result<StrategyProfile, SolverError>::success(std::move(profile));
  }
  if (game.game_id != "kuhn_poker_v1") {
    return Result<StrategyProfile, SolverError>::failure(SolverError::UnsupportedAlgorithm);
  }

  set_binary_strategy(profile, "kuhn:p0:J:", {action_check, 2.0 / 3.0},
                      {action_bet_or_raise, 1.0 / 3.0});
  set_binary_strategy(profile, "kuhn:p0:Q:", {action_check, 1.0}, {action_bet_or_raise, 0.0});
  set_binary_strategy(profile, "kuhn:p0:K:", {action_check, 0.0}, {action_bet_or_raise, 1.0});
  set_binary_strategy(profile, "kuhn:p0:J:kb", {action_fold, 1.0}, {action_call, 0.0});
  set_binary_strategy(profile, "kuhn:p0:Q:kb", {action_fold, 1.0 / 3.0}, {action_call, 2.0 / 3.0});
  set_binary_strategy(profile, "kuhn:p0:K:kb", {action_fold, 0.0}, {action_call, 1.0});

  set_binary_strategy(profile, "kuhn:p1:J:k", {action_check, 2.0 / 3.0},
                      {action_bet_or_raise, 1.0 / 3.0});
  set_binary_strategy(profile, "kuhn:p1:Q:k", {action_check, 1.0}, {action_bet_or_raise, 0.0});
  set_binary_strategy(profile, "kuhn:p1:K:k", {action_check, 0.0}, {action_bet_or_raise, 1.0});
  set_binary_strategy(profile, "kuhn:p1:J:b", {action_fold, 1.0}, {action_call, 0.0});
  set_binary_strategy(profile, "kuhn:p1:Q:b", {action_fold, 2.0 / 3.0}, {action_call, 1.0 / 3.0});
  set_binary_strategy(profile, "kuhn:p1:K:b", {action_fold, 0.0}, {action_call, 1.0});
  return Result<StrategyProfile, SolverError>::success(std::move(profile));
}

} // namespace gtosd
