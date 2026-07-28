#include "gtosd/core/cards.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/core/money.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t assertion_count = 0;

void require(const bool condition, const std::string_view message) {
  ++assertion_count;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::Money units(const std::int64_t value) { return gtosd::Money::from_units(value).value(); }

gtosd::Money antes(const std::int64_t value) { return gtosd::Money::from_antes(value).value(); }

gtosd::PotPercentage pct(const std::int64_t value) {
  return gtosd::PotPercentage::from_basis_points(value).value();
}

gtosd::RangeWeight weight(const std::int64_t value) {
  return gtosd::RangeWeight::from_basis_points(value).value();
}

gtosd::ActionConfig config(const std::vector<std::int64_t> &sizes, const std::uint8_t raise_depth,
                           const gtosd::AllInMode mode = gtosd::AllInMode::Disabled,
                           const std::int64_t threshold_bp = 0,
                           const gtosd::Money minimum_bet = units(1)) {
  gtosd::ActionConfig result;
  for (const auto size : sizes) {
    result.aggressive_sizes.push_back(pct(size));
  }
  result.raise_depth = raise_depth;
  result.all_in_mode = mode;
  result.all_in_threshold = pct(threshold_bp);
  result.minimum_bet = minimum_bet;
  return result;
}

bool has_type(const std::vector<gtosd::Action> &actions, const gtosd::ActionType type) {
  return std::ranges::any_of(actions, [&](const auto &action) { return action.type == type; });
}

std::size_t count_aggressive(const std::vector<gtosd::Action> &actions) {
  return static_cast<std::size_t>(std::ranges::count_if(actions, [](const auto &action) {
    return action.type == gtosd::ActionType::Bet || action.type == gtosd::ActionType::Raise ||
           action.type == gtosd::ActionType::AllIn;
  }));
}

gtosd::PublicState scenario_state(const gtosd::Street street, const std::uint8_t actor,
                                  const bool facing_bet,
                                  const gtosd::Money actor_stack = antes(300)) {
  gtosd::PublicState state;
  state.street = street;
  state.player_to_act = actor;
  state.initial_pot = antes(100);
  state.pot = antes(100);
  state.initial_pot_contributions[0] = antes(50);
  state.initial_pot_contributions[1] = antes(50);
  state.remaining_stacks[0] = antes(300);
  state.remaining_stacks[1] = antes(300);
  state.remaining_stacks[actor] = actor_stack;
  if (facing_bet) {
    const auto bettor = static_cast<std::uint8_t>(actor ^ 1U);
    state.pot = antes(150);
    state.current_bet = antes(50);
    state.last_full_raise_increment = antes(50);
    state.remaining_stacks[bettor] = antes(250);
    state.committed_this_street[bettor] = antes(50);
    state.committed_total[bettor] = antes(50);
    state.acted_players_mask = static_cast<std::uint8_t>(1U << bettor);
  } else if (actor == static_cast<std::uint8_t>(gtosd::Player::BTN)) {
    state.acted_players_mask =
        static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(gtosd::Player::CO));
  }
  require(gtosd::validate_state(state).has_value(), "scenario state valid");
  return state;
}

void test_cards_section_22_2() {
  static_assert(sizeof(gtosd::CardId) == 1U);
  const auto deck = gtosd::short_deck();
  std::vector<gtosd::CardId> all(deck.begin(), deck.end());
  const auto full_mask = gtosd::card_mask(all);
  require(full_mask.has_value() && std::popcount(full_mask.value()) == 36,
          "deck has 36 unique CardId values");

  const auto ace_spades = gtosd::parse_card("As");
  require(ace_spades.has_value() && ace_spades.value().rank() == gtosd::Rank::Ace &&
              ace_spades.value().suit() == gtosd::Suit::Spades,
          "As parses as ace of spades");
  for (const char rank : std::string_view{"2345"}) {
    for (const char suit : std::string_view{"cdhs"}) {
      const std::string invalid{rank, suit};
      require(!gtosd::parse_card(invalid), "ranks 2 through 5 rejected");
    }
  }
  require(!gtosd::parse_card("AS") && !gtosd::parse_card("A") && !gtosd::parse_card("Ass"),
          "malformed cards rejected");

  std::vector<gtosd::CardId> seven(deck.begin(), deck.begin() + 7);
  require(std::popcount(gtosd::remaining_deck_mask(seven).value()) == 29,
          "remaining deck after seven cards has popcount 29");
  seven.push_back(seven.front());
  require(!gtosd::card_mask(seven) && !gtosd::remaining_deck_mask(seven),
          "duplicate cards rejected");

  for (std::uint8_t index = 0; index < 36U; ++index) {
    const auto card = gtosd::CardId::from_index(index);
    require(card.has_value() && card.value().value() == index, "CardId index round-trip");
    require(gtosd::parse_card(gtosd::format_card(card.value())).value() == card.value(),
            "CardId text round-trip");
  }
  require(!gtosd::CardId::from_index(36), "CardId 36 rejected");

  std::mt19937 rng(0xC0FFEEU);
  for (std::size_t permutation = 0; permutation < 1'000U; ++permutation) {
    std::shuffle(all.begin(), all.end(), rng);
    std::uint64_t reparsed_mask = 0;
    for (const auto card : all) {
      const auto reparsed = gtosd::parse_card(gtosd::format_card(card));
      require(reparsed.has_value(), "permutation card reparses");
      reparsed_mask |= reparsed.value().mask();
    }
    require(reparsed_mask == full_mask.value(),
            "random permutation preserves complete physical deck");
  }
}

void test_fixed_point_section_22_4() {
  static_assert(sizeof(gtosd::Money) == 8U);
  require(!gtosd::Money::from_units(-1), "negative money rejected");
  require(!gtosd::RangeWeight::from_basis_points(-1) &&
              !gtosd::PotPercentage::from_basis_points(-1),
          "negative percentages rejected");
  require(!gtosd::RangeWeight::from_basis_points(10'001) &&
              !gtosd::PotPercentage::from_basis_points(100'001),
          "percentage maxima enforced");
  require(gtosd::format_money(units(123'456)) == "12.3456", "money formatting keeps four decimals");

  require(gtosd::percent_of(antes(100), 5'000, 100'000).value() == antes(50),
          "pot 100 bet 50 percent equals 50");
  require(gtosd::percent_of(antes(3), 3'333, 100'000).value() == units(9'999),
          "pot 3 bet 33.33 percent equals 0.9999");

  auto raise_state = scenario_state(gtosd::Street::Flop, 0, true, antes(300));
  const auto raise_actions = gtosd::legal_actions(raise_state, config({5'000}, 1)).value();
  const auto raise = std::ranges::find_if(
      raise_actions, [](const auto &action) { return action.type == gtosd::ActionType::Raise; });
  require(raise != raise_actions.end() && raise->amount == antes(150),
          "pot 150 call 50 raise 50 percent has total action 150");

  auto shallow = scenario_state(gtosd::Street::Flop, 0, false, antes(40));
  const auto capped = gtosd::legal_actions(shallow, config({5'000}, 0)).value();
  require(has_type(capped, gtosd::ActionType::AllIn) && !has_type(capped, gtosd::ActionType::Bet),
          "size beyond stack converts to all-in");

  auto tiny = scenario_state(gtosd::Street::Flop, 0, false, units(10));
  tiny.initial_pot = units(1);
  tiny.pot = units(1);
  tiny.initial_pot_contributions[0] = units(1);
  tiny.initial_pot_contributions[1] = {};
  require(gtosd::validate_state(tiny).has_value(), "tiny state valid");
  const auto deduplicated = gtosd::legal_actions(tiny, config({5'000, 5'001}, 0)).value();
  require(count_aggressive(deduplicated) == 1U, "rounded duplicate sizes produce one action");

  require(!gtosd::PotPercentage::from_basis_points(100'100), "1001 percent overbet rejected");
  const auto near_limit = units(std::numeric_limits<std::int64_t>::max() / 10);
  const auto scaled = gtosd::percent_of(near_limit, 100'000, 100'000);
  require(scaled.has_value() && scaled.value().units() > 0,
          "near-limit multiplication succeeds without overflow");
  require(!gtosd::percent_of(units(std::numeric_limits<std::int64_t>::max()), 100'000, 100'000),
          "true multiplication overflow is explicit");

  for (const auto &[push_units, triggers] : std::array<std::pair<std::int64_t, bool>, 3>{
           std::pair{999'900, true}, std::pair{1'000'000, false}, std::pair{1'000'100, false}}) {
    auto boundary = scenario_state(gtosd::Street::Flop, 0, false, units(push_units));
    boundary.initial_pot = antes(100);
    boundary.pot = antes(100);
    const auto go =
        gtosd::legal_actions(boundary, config({5'000}, 0, gtosd::AllInMode::Go, 10'000));
    require(go.has_value(), "all-in boundary actions generated");
    require(has_type(go.value(), gtosd::ActionType::AllIn) == triggers,
            "strict all-in threshold boundary");
    require(has_type(go.value(), gtosd::ActionType::Bet) != triggers,
            "Go all-in replaces regular aggression only below threshold");
    require(has_type(go.value(), gtosd::ActionType::Check), "Go all-in preserves check");
  }

  auto add_state = scenario_state(gtosd::Street::Flop, 0, false, antes(40));
  const auto add =
      gtosd::legal_actions(add_state, config({2'500}, 0, gtosd::AllInMode::Add, 10'000)).value();
  require(has_type(add, gtosd::ActionType::Bet) && has_type(add, gtosd::ActionType::AllIn) &&
              has_type(add, gtosd::ActionType::Check),
          "Add all-in keeps passive and regular aggressive actions");
  const auto explicit_push = std::ranges::find_if(
      add, [](const auto &action) { return action.type == gtosd::ActionType::AllIn; });
  require(explicit_push != add.end() && explicit_push->all_in_kind == gtosd::AllInKind::Raise,
          "aggressive all-in is explicitly classified as raise");

  auto facing = scenario_state(gtosd::Street::Flop, 0, true, antes(60));
  const auto go_facing =
      gtosd::legal_actions(facing, config({5'000}, 1, gtosd::AllInMode::Go, 10'000)).value();
  require(has_type(go_facing, gtosd::ActionType::Fold) &&
              has_type(go_facing, gtosd::ActionType::Call),
          "Go all-in preserves fold and call");

  const auto below_min_raise = gtosd::legal_actions(raise_state, config({1'000}, 1)).value();
  require(!has_type(below_min_raise, gtosd::ActionType::Raise),
          "raise increment below last full raise is discarded");
  auto open_state = scenario_state(gtosd::Street::Flop, 0, false);
  const auto below_min_bet =
      gtosd::legal_actions(open_state, config({50}, 0, gtosd::AllInMode::Disabled, 0, antes(1)))
          .value();
  require(!has_type(below_min_bet, gtosd::ActionType::Bet),
          "bet below configured minimum is discarded");
}

void test_rake_section_22_5() {
  const gtosd::RakeConfig disabled{};
  const gtosd::RakeConfig rake{true, weight(500), antes(3), true, {}};
  require(gtosd::calculate_rake(disabled, antes(100), true).value().units() == 0,
          "disabled rake is zero");
  require(gtosd::calculate_rake(rake, antes(100), true).value() == antes(3),
          "five percent pot 100 capped at 3");
  require(gtosd::calculate_rake(rake, antes(20), true).value() == antes(1),
          "five percent pot 20 equals 1");
  require(gtosd::calculate_rake(rake, antes(100), false).value().units() == 0,
          "no flop no drop preflop is zero");
  const gtosd::RakeConfig preflop_rake{true, weight(500), antes(3), false, {}};
  require(gtosd::calculate_rake(preflop_rake, antes(20), false).value() == antes(1),
          "preflop rake applies when no-flop-no-drop disabled");

  auto postflop =
      gtosd::make_hu_postflop_state(gtosd::Street::Flop, antes(100), antes(100)).value();
  const auto bet_config = config({5'000}, 0);
  const auto bet_actions = gtosd::legal_actions(postflop, bet_config).value();
  const auto bet = *std::ranges::find_if(
      bet_actions, [](const auto &action) { return action.type == gtosd::ActionType::Bet; });
  postflop = gtosd::apply_action(postflop, bet, bet_config).value();
  const auto fold_actions = gtosd::legal_actions(postflop, bet_config).value();
  const auto fold = *std::ranges::find_if(
      fold_actions, [](const auto &action) { return action.type == gtosd::ActionType::Fold; });
  postflop = gtosd::apply_action(postflop, fold, bet_config).value();
  require(postflop.pot == antes(100) && postflop.returned_uncalled == antes(50),
          "fold returns uncalled bet before rake");
  const auto folded_settlement = gtosd::settle_terminal(postflop, rake).value();
  require(folded_settlement.called_pot == antes(100) && folded_settlement.rake == antes(3),
          "rake uses called pot only");

  const auto split = gtosd::split_pot(units(10), units(1), gtosd::WinnerMask{0b111}, 3).value();
  require(split[0].units() == 3 && split[1].units() == 3 && split[2].units() == 3,
          "rake removed before split");
  const auto remainder = gtosd::split_pot(units(10), {}, gtosd::WinnerMask{0b111}, 3).value();
  require(remainder[0].units() == 4 && remainder[1].units() == 3 && remainder[2].units() == 3,
          "fixed-point remainder goes to lowest winner seat");

  const auto ante = antes(1);
  const auto root = gtosd::make_hu_preflop_state(antes(40), ante).value();
  const auto root_config = config({5'000}, 1, gtosd::AllInMode::Disabled, 0, ante);
  const auto root_actions = gtosd::legal_actions(root, root_config).value();
  auto folded = gtosd::apply_action(root, root_actions.front(), root_config).value();
  require(folded.pot == antes(2) && folded.returned_uncalled == ante,
          "preflop fold returns unmatched button blind");
  const auto no_drop = gtosd::settle_terminal(folded, rake).value();
  require(no_drop.rake.units() == 0, "preflop fold has no-flop-no-drop");
  require(no_drop.payoff_units[0] + no_drop.payoff_units[1] == 0,
          "rake-free HU payoff sum is zero");
  const auto with_drop = gtosd::settle_terminal(folded, preflop_rake).value();
  require(with_drop.payoff_units[0] + with_drop.payoff_units[1] == -with_drop.rake.units(),
          "HU payoff sum equals negative rake");
}

void test_legal_action_matrix_section_22_6() {
  std::size_t combinations = 0;
  for (std::uint8_t street_index = 0; street_index < 4U; ++street_index) {
    const auto street = static_cast<gtosd::Street>(street_index);
    for (std::uint8_t actor = 0; actor < 2U; ++actor) {
      for (const bool facing_bet : {false, true}) {
        for (std::uint8_t depth = 0; depth <= 4U; ++depth) {
          for (const auto mode :
               {gtosd::AllInMode::Disabled, gtosd::AllInMode::Add, gtosd::AllInMode::Go}) {
            ++combinations;
            const auto state = scenario_state(street, actor, facing_bet);
            const auto action_config = config({5'000}, depth, mode, 40'000, antes(1));
            const auto generated = gtosd::legal_actions(state, action_config);
            require(generated.has_value(), "parameterized legal actions generated");
            const auto &actions = generated.value();
            require(has_type(actions, gtosd::ActionType::Check) == !facing_bet,
                    "check only without call amount");
            require(has_type(actions, gtosd::ActionType::Call) == facing_bet,
                    "call only with call amount");
            require(has_type(actions, gtosd::ActionType::Fold) == facing_bet,
                    "fold only when facing aggression");
            require(!facing_bet || !has_type(actions, gtosd::ActionType::Bet),
                    "bet not offered against bet");
            require(facing_bet || !has_type(actions, gtosd::ActionType::Raise),
                    "raise not offered without bet");
            if (facing_bet && depth == 0U) {
              require(!has_type(actions, gtosd::ActionType::Raise),
                      "raise depth zero forbids non-all-in raise");
            }
            for (const auto &action : actions) {
              const auto next = gtosd::apply_action(state, action, action_config);
              require(next.has_value(), "every generated action applies");
              require(gtosd::validate_state(next.value()).has_value(),
                      "every action preserves state invariants");
              for (std::uint8_t player = 0; player < 2U; ++player) {
                require(next.value().remaining_stacks[player].units() >= 0, "stack never negative");
              }
            }
          }
        }
      }
    }
  }
  require(combinations == 240U, "4 street x 2 player x 2 scenario x 5 depth x 3 modes covered");

  auto short_call = scenario_state(gtosd::Street::Turn, 0, true, antes(30));
  short_call.all_in_players_mask = 0;
  const auto call_config = config({5'000}, 4);
  const auto short_actions = gtosd::legal_actions(short_call, call_config).value();
  const auto call = std::ranges::find_if(
      short_actions, [](const auto &action) { return action.type == gtosd::ActionType::Call; });
  require(call != short_actions.end() && call->all_in_kind == gtosd::AllInKind::Call &&
              call->amount == antes(30),
          "all-in call is distinct and stack-capped");
  const auto called = gtosd::apply_action(short_call, *call, call_config).value();
  require(called.status == gtosd::HandStatus::AllInRunout && called.returned_uncalled == antes(20),
          "short all-in call closes street and returns unmatched amount");

  auto all_in_actor = scenario_state(gtosd::Street::Flop, 0, false);
  all_in_actor.remaining_stacks[0] = {};
  all_in_actor.all_in_players_mask = 0b01;
  require(!gtosd::validate_state(all_in_actor) ||
              !gtosd::legal_actions(all_in_actor, config({5'000}, 1)),
          "all-in player is never asked to act");

  auto checked = scenario_state(gtosd::Street::Flop, 0, false);
  const auto passive_config = config({}, 0);
  checked = gtosd::apply_action(checked, gtosd::legal_actions(checked, passive_config).value()[0],
                                passive_config)
                .value();
  require(checked.status == gtosd::HandStatus::InProgress && checked.player_to_act == 1U,
          "first check passes action");
  checked = gtosd::apply_action(checked, gtosd::legal_actions(checked, passive_config).value()[0],
                                passive_config)
                .value();
  require(checked.status == gtosd::HandStatus::StreetComplete,
          "check-check closes non-river street");
  const auto turn = gtosd::advance_street(checked).value();
  require(turn.street == gtosd::Street::Turn && turn.status == gtosd::HandStatus::InProgress &&
              turn.player_to_act == 0U && turn.committed_this_street[0].units() == 0 &&
              turn.committed_this_street[1].units() == 0,
          "advance street resets street-local state and restores CO first");

  auto root = gtosd::make_hu_preflop_state(antes(40), antes(1)).value();
  const auto root_config = config({}, 0, gtosd::AllInMode::Disabled, 0, antes(1));
  const auto root_actions = gtosd::legal_actions(root, root_config).value();
  root = gtosd::apply_action(root, root_actions[1], root_config).value();
  require(root.status == gtosd::HandStatus::InProgress && root.player_to_act == 1U,
          "CO root call preserves BTN check option");
  const auto btn_actions = gtosd::legal_actions(root, root_config).value();
  require(has_type(btn_actions, gtosd::ActionType::Check), "BTN can check after CO root call");
  root = gtosd::apply_action(root, btn_actions.front(), root_config).value();
  require(root.status == gtosd::HandStatus::StreetComplete, "BTN check closes preflop street");

  auto capped_depth = scenario_state(gtosd::Street::River, 0, true);
  capped_depth.raise_count_this_street = 4;
  const auto capped_actions = gtosd::legal_actions(capped_depth, config({5'000}, 4)).value();
  require(!has_type(capped_actions, gtosd::ActionType::Raise),
          "raise not offered at configured depth");

  require(!gtosd::legal_actions(capped_depth, config({1'000, 2'000, 3'000, 4'000}, 4)),
          "more than three configured sizes rejected");
  auto invalid_depth = config({5'000}, 4);
  invalid_depth.raise_depth = 5;
  require(!gtosd::legal_actions(capped_depth, invalid_depth), "raise depth above four rejected");

  const gtosd::Action forged{gtosd::ActionType::Raise, antes(1), gtosd::AllInKind::None, 5'000};
  require(!gtosd::apply_action(capped_depth, forged, config({5'000}, 4)),
          "action applier rejects actions not generated as legal");
}

void property_test_100000_legal_transitions() {
  std::mt19937_64 rng(0x5EED'F1ULL);
  const auto action_config =
      config({2'500, 5'000, 10'000}, 4, gtosd::AllInMode::Add, 10'000, antes(1));
  auto state = gtosd::make_hu_postflop_state(gtosd::Street::Flop, antes(10), antes(100)).value();
  constexpr std::size_t target_transitions = 100'000;
  std::size_t completed = 0;
  while (completed < target_transitions) {
    if (state.status == gtosd::HandStatus::StreetComplete) {
      state = gtosd::advance_street(state).value();
      continue;
    }
    if (state.status != gtosd::HandStatus::InProgress) {
      state = gtosd::make_hu_postflop_state(gtosd::Street::Flop, antes(10), antes(100)).value();
      continue;
    }
    const auto legal = gtosd::legal_actions(state, action_config);
    require(legal.has_value() && !legal.value().empty(), "property state always has legal action");
    const auto selected =
        static_cast<std::size_t>(rng() % static_cast<std::uint64_t>(legal.value().size()));
    const auto next = gtosd::apply_action(state, legal.value()[selected], action_config);
    require(next.has_value(), "random legal action applies");
    require(gtosd::validate_state(next.value()).has_value(),
            "random transition preserves chip invariants");
    require(gtosd::serialize_public_state(next.value()) ==
                gtosd::serialize_public_state(next.value()),
            "state serialization deterministic");
    for (std::uint8_t player = 0; player < 2U; ++player) {
      require(next.value().remaining_stacks[player].units() >= 0 &&
                  next.value().committed_total[player].units() >= 0,
              "random transition never creates negative chips");
    }
    state = next.value();
    ++completed;
  }
  require(completed == target_transitions, "100000 random legal transitions completed");
}

void test_serialization() {
  const auto state = gtosd::make_hu_preflop_state(antes(40), antes(1)).value();
  const auto first = gtosd::serialize_public_state(state);
  const auto second = gtosd::serialize_public_state(state);
  require(first == second, "identical state serializes identically");
  require(first.starts_with("GTOSD-PUBLIC-1|0|0|"), "serialization is versioned and deterministic");
  auto changed = state;
  changed.board_mask = 1;
  require(gtosd::serialize_public_state(changed) != first,
          "public-state changes affect serialization");
}

} // namespace

int main() {
  try {
    test_cards_section_22_2();
    test_fixed_point_section_22_4();
    test_rake_section_22_5();
    test_legal_action_matrix_section_22_6();
    property_test_100000_legal_transitions();
    test_serialization();
    std::cout << "F1_RULE_TESTS=PASS assertions=" << assertion_count
              << " randomized_transitions=100000 parameterized_combinations=240\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F1_RULE_TESTS=FAIL assertion=" << assertion_count << " reason=" << error.what()
              << '\n';
    return 1;
  }
}
