#include "gtosd/solver/reference_games.hpp"

#include "gtosd/equity/showdown.hpp"

#include <algorithm>
#include <array>
#include <bit>
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
constexpr GameActionId action_all_in = 4;

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

constexpr double four_street_stack = 8.0;
constexpr double four_street_postflop_bet = 2.0;
constexpr std::uint8_t four_street_flop_outcomes = 1U;
constexpr std::uint8_t four_street_turn_outcomes = 2U;
constexpr std::uint8_t four_street_river_outcomes = 1U;

struct FourStreetBoards {
  std::array<std::array<CardId, 3>, 2> flops{};
  std::array<std::array<CardId, 2>, 2> turns{};
  std::array<std::array<std::array<CardId, 2>, 2>, 2> rivers{};
};

struct FourStreetState {
  std::array<std::array<CardId, 2>, 2> holes{};
  std::array<std::uint8_t, 2> private_types{};
  std::vector<CardId> board;
  std::uint8_t flop_branch{0};
  std::uint8_t turn_branch{0};
  std::array<double, 2> contributions{1.0, 2.0};
  std::string history{"p"};
  double rake_fraction{0.0};
};

std::array<double, 2> four_street_winner_payoff(const std::uint8_t winner,
                                                const std::array<double, 2> contributions) {
  const double pot = contributions[0] + contributions[1];
  std::array<double, 2> payoff{-contributions[0], -contributions[1]};
  payoff[winner] += pot;
  return payoff;
}

std::array<double, 2> four_street_showdown_payoff(const FourStreetState &state) {
  const auto showdown = evaluate_showdown(
      std::vector<std::array<CardId, 2>>{state.holes[0], state.holes[1]}, state.board);
  if (!showdown || state.board.size() != 5U || showdown.value().winner_mask == 0U) {
    throw std::logic_error("invalid four-street Short Deck showdown fixture");
  }
  const double pot = state.contributions[0] + state.contributions[1];
  const double distributable = pot * (1.0 - state.rake_fraction);
  std::array<double, 2> payoff{-state.contributions[0], -state.contributions[1]};
  const auto winner_count = static_cast<unsigned>(std::popcount(showdown.value().winner_mask));
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    if ((showdown.value().winner_mask & (std::uint8_t{1} << player)) != 0U) {
      payoff[player] += distributable / static_cast<double>(winner_count);
    }
  }
  return payoff;
}

std::string four_street_information_set(const FourStreetState &state, const std::uint8_t player,
                                        const std::string_view suffix) {
  std::string key = "short_deck_four_street:p" + std::to_string(player) + ":h" +
                    std::to_string(state.private_types[player]) + ":b";
  for (const auto card : state.board) {
    key += format_card(card);
  }
  key += ":c" + std::to_string(static_cast<unsigned>(state.contributions[0])) + "_" +
         std::to_string(static_cast<unsigned>(state.contributions[1])) + ":" + state.history +
         ":" + std::string(suffix);
  return key;
}

char four_street_street_code(const FourStreetState &state) {
  switch (state.board.size()) {
  case 3U:
    return 'f';
  case 4U:
    return 't';
  case 5U:
    return 'r';
  default:
    throw std::logic_error("invalid four-street Short Deck board size");
  }
}

GameNodeId build_four_street_public_chance(GameBuilder &builder, const FourStreetBoards &boards,
                                           const FourStreetState &state, bool all_in);

GameNodeId build_four_street_after_round(GameBuilder &builder, const FourStreetBoards &boards,
                                         const FourStreetState &state) {
  if (state.board.size() == 5U) {
    return builder.terminal(four_street_showdown_payoff(state));
  }
  const bool all_in = state.contributions[0] >= four_street_stack &&
                      state.contributions[1] >= four_street_stack;
  return build_four_street_public_chance(builder, boards, state, all_in);
}

GameNodeId build_four_street_betting(GameBuilder &builder, const FourStreetBoards &boards,
                                     const FourStreetState &state) {
  const double remaining = four_street_stack - state.contributions[0];
  if (remaining <= 0.0) {
    return build_four_street_after_round(builder, boards, state);
  }
  const double bet = std::min(four_street_postflop_bet, remaining);
  const bool bet_is_all_in = bet == remaining;
  const auto aggressive_action = bet_is_all_in ? action_all_in : action_bet_or_raise;
  const char street = four_street_street_code(state);

  FourStreetState first_bet = state;
  first_bet.contributions[1] += bet;
  first_bet.history += "/" + std::string(1, street) + ":b1";
  const auto first_bet_fold =
      builder.terminal(four_street_winner_payoff(1U, first_bet.contributions));
  FourStreetState first_bet_call = first_bet;
  first_bet_call.contributions[0] += bet;
  first_bet_call.history += "c0";
  const auto first_bet_called = build_four_street_after_round(builder, boards, first_bet_call);
  const auto first_bet_response = builder.decision(
      0U, four_street_information_set(first_bet, 0U, "facing_bet"),
      {edge(action_fold, "fold", first_bet_fold),
       edge(action_call, "call", first_bet_called)});

  FourStreetState first_check = state;
  first_check.history += "/" + std::string(1, street) + ":k1";
  const auto checked_through = build_four_street_after_round(builder, boards, first_check);

  return builder.decision(
      1U, four_street_information_set(state, 1U, "first_to_act"),
      {edge(action_check, "check", checked_through),
       edge(aggressive_action, bet_is_all_in ? "all_in" : "bet", first_bet_response)});
}

GameNodeId build_four_street_public_chance(GameBuilder &builder, const FourStreetBoards &boards,
                                           const FourStreetState &state, const bool all_in) {
  std::vector<GameEdge> outcomes;
  outcomes.reserve(2U);
  if (state.board.empty()) {
    for (std::uint8_t branch = 0U; branch < four_street_flop_outcomes; ++branch) {
      FourStreetState next = state;
      next.flop_branch = branch;
      next.board.assign(boards.flops[branch].begin(), boards.flops[branch].end());
      next.history += "/F" + std::to_string(branch);
      const auto child = all_in ? build_four_street_public_chance(builder, boards, next, true)
                                : build_four_street_betting(builder, boards, next);
      outcomes.push_back(edge(600U + branch, ("flop_" + std::to_string(branch)).c_str(), child,
                              1.0 / static_cast<double>(four_street_flop_outcomes)));
    }
  } else if (state.board.size() == 3U) {
    for (std::uint8_t branch = 0U; branch < four_street_turn_outcomes; ++branch) {
      FourStreetState next = state;
      next.turn_branch = branch;
      next.board.push_back(boards.turns[state.flop_branch][branch]);
      next.history += "/T" + std::to_string(branch);
      const auto child = all_in ? build_four_street_public_chance(builder, boards, next, true)
                                : build_four_street_betting(builder, boards, next);
      outcomes.push_back(edge(610U + branch, ("turn_" + std::to_string(branch)).c_str(), child,
                              1.0 / static_cast<double>(four_street_turn_outcomes)));
    }
  } else if (state.board.size() == 4U) {
    const auto &rivers = boards.rivers[state.flop_branch][state.turn_branch];
    for (std::uint8_t branch = 0U; branch < four_street_river_outcomes; ++branch) {
      FourStreetState next = state;
      next.board.push_back(rivers[branch]);
      next.history += "/R" + std::to_string(branch);
      const auto child = all_in ? builder.terminal(four_street_showdown_payoff(next))
                                : build_four_street_betting(builder, boards, next);
      outcomes.push_back(edge(620U + branch, ("river_" + std::to_string(branch)).c_str(), child,
                              1.0 / static_cast<double>(four_street_river_outcomes)));
    }
  } else {
    throw std::logic_error("invalid four-street Short Deck public chance state");
  }
  return builder.chance(std::move(outcomes));
}

GameNodeId build_four_street_preflop(GameBuilder &builder, const FourStreetBoards &boards,
                                     const FourStreetState &state) {
  const auto root_fold = builder.terminal(four_street_winner_payoff(1U, state.contributions));

  FourStreetState called = state;
  called.contributions[0] = 2.0;
  called.history += ":c0";
  FourStreetState checked = called;
  checked.history += "k1";
  const auto call_check = build_four_street_public_chance(builder, boards, checked, false);
  FourStreetState limp_raised = called;
  limp_raised.contributions[1] = 4.0;
  limp_raised.history += "r1";
  const auto limp_raise_fold =
      builder.terminal(four_street_winner_payoff(1U, limp_raised.contributions));
  FourStreetState limp_raise_called = limp_raised;
  limp_raise_called.contributions[0] = 4.0;
  limp_raise_called.history += "c0";
  const auto limp_raise_call =
      build_four_street_public_chance(builder, boards, limp_raise_called, false);
  const auto limp_raise_response = builder.decision(
      0U, four_street_information_set(limp_raised, 0U, "facing_limp_raise"),
      {edge(action_fold, "fold", limp_raise_fold),
       edge(action_call, "call", limp_raise_call)});
  const auto after_call = builder.decision(
      1U, four_street_information_set(called, 1U, "after_call"),
      {edge(action_check, "check", call_check),
       edge(action_bet_or_raise, "raise", limp_raise_response)});

  FourStreetState raised = state;
  raised.contributions[0] = 4.0;
  raised.history += ":r0";
  const auto raise_fold = builder.terminal(four_street_winner_payoff(0U, raised.contributions));
  FourStreetState raise_called = raised;
  raise_called.contributions[1] = 4.0;
  raise_called.history += "c1";
  const auto raise_call = build_four_street_public_chance(builder, boards, raise_called, false);
  const auto raise_response = builder.decision(
      1U, four_street_information_set(raised, 1U, "facing_raise"),
      {edge(action_fold, "fold", raise_fold), edge(action_call, "call", raise_call)});

  FourStreetState shoved = state;
  shoved.contributions[0] = four_street_stack;
  shoved.history += ":a0";
  const auto shove_fold = builder.terminal(four_street_winner_payoff(0U, shoved.contributions));
  FourStreetState shove_called = shoved;
  shove_called.contributions[1] = four_street_stack;
  shove_called.history += "c1";
  const auto shove_call = build_four_street_public_chance(builder, boards, shove_called, true);
  const auto shove_response = builder.decision(
      1U, four_street_information_set(shoved, 1U, "facing_all_in"),
      {edge(action_fold, "fold", shove_fold), edge(action_call, "call", shove_call)});

  return builder.decision(
      0U, four_street_information_set(state, 0U, "root"),
      {edge(action_fold, "fold", root_fold), edge(action_call, "call", after_call),
       edge(action_bet_or_raise, "raise", raise_response),
       edge(action_all_in, "all_in", shove_response)});
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

Result<FiniteGame, SolverError>
make_short_deck_four_street_toy_game(const double rake_fraction) {
  if (!std::isfinite(rake_fraction) || rake_fraction < 0.0 || rake_fraction > 0.25) {
    return Result<FiniteGame, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  const auto card = [](const std::string_view text) { return parse_card(text).value(); };
  const std::array<std::array<CardId, 2>, 2> first_hands{{
      {card("As"), card("Ah")},
      {card("Qc"), card("Jc")},
  }};
  const std::array<std::array<CardId, 2>, 2> second_hands{{
      {card("Kd"), card("Kh")},
      {card("Td"), card("9d")},
  }};

  FourStreetBoards boards;
  boards.flops = {{{card("8s"), card("7h"), card("6c")},
                   {card("Qs"), card("Jh"), card("Tc")}}};
  boards.turns = {{{card("9c"), card("Ad")}, {card("8h"), card("6s")}}};
  boards.rivers[0][0] = {card("Ts"), card("Qd")};
  boards.rivers[0][1] = {card("6d"), card("Js")};
  boards.rivers[1][0] = {card("7s"), card("Ac")};
  boards.rivers[1][1] = {card("9h"), card("Ks")};

  GameBuilder builder;
  std::vector<GameEdge> deals;
  deals.reserve(first_hands.size() * second_hands.size());
  GameActionId deal_action = 700U;
  for (std::uint8_t first_type = 0U; first_type < first_hands.size(); ++first_type) {
    for (std::uint8_t second_type = 0U; second_type < second_hands.size(); ++second_type) {
      FourStreetState state;
      state.holes = {first_hands[first_type], second_hands[second_type]};
      state.private_types = {first_type, second_type};
      state.rake_fraction = rake_fraction;
      const auto label = "deal_" + std::to_string(first_type) + "_" +
                         std::to_string(second_type);
      deals.push_back(edge(deal_action++, label.c_str(),
                           build_four_street_preflop(builder, boards, state), 0.25));
    }
  }
  FiniteGame game{"short_deck_four_street_toy_v1_rake_" + std::to_string(rake_fraction),
                  builder.chance(std::move(deals)), builder.finish(), 3.0};
  const auto valid = validate_finite_game(game);
  return valid ? Result<FiniteGame, SolverError>::success(std::move(game))
               : Result<FiniteGame, SolverError>::failure(valid.error());
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
